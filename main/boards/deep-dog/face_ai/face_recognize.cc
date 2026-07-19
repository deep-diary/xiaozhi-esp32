/**
 * Deep-dog 本地人脸数字 ID（S04）：HumanFaceRecognizer + facedb FAT + NVS 元数据。
 * S05 可写 Immich 真名到 meta；禁止在 httpd 回调内推理。
 */
#include "sdkconfig.h"

#include "face_ai_config.h"
#include "face_recognize.h"
#include "face_ai_types.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <esp_heap_caps.h>
#include <esp_check.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs.h>
#include <nvs_flash.h>

#if DEEP_DOG_FACE_AI_ENABLE && DEEP_DOG_FACE_RECOG_ENABLE && defined(CONFIG_IDF_TARGET_ESP32S3)

#include "esp_vfs_fat.h"
#include "human_face_recognition.hpp"
#include "wear_levelling.h"

#define TAG "dog_face_rec"

bool DeepDogFaceRecognizeInit();
bool DeepDogFaceRecognizeReady();

static HumanFaceRecognizer* s_recog = nullptr;
static wl_handle_t s_wl = WL_INVALID_HANDLE;
static bool s_fs_mounted = false;
static bool s_ready = false;

/** meta_ver=2：display_name 扩到 32。旧 blob 尺寸不匹配则丢弃并按 facedb 重建。 */
static constexpr uint8_t kFaceMetaVer = 2;

struct FaceMeta {
    int local_id = 0;
    char display_name[32] = {};
    uint32_t updated_at = 0;
    char immich_person_id[40] = {};
};

static bool IsPlaceholderName(int local_id, const char* name) {
    if (!name || name[0] == '\0') {
        return true;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "#%d", local_id);
    return strcmp(name, buf) == 0;
}

static FaceMeta s_meta[DEEP_DOG_FACE_RECOG_MAX]{};
static int s_meta_count = 0;
static int s_next_id = 1;

/** 会话：最近一次成功识别的 feat 向量副本 */
static float* s_session_feat = nullptr;
static int s_session_feat_len = 0;
static int s_session_id = 0;
static int64_t s_session_us = 0;

static float CosineSim(const float* a, const float* b, int n) {
    double dot = 0, na = 0, nb = 0;
    for (int i = 0; i < n; i++) {
        dot += (double)a[i] * (double)b[i];
        na += (double)a[i] * (double)a[i];
        nb += (double)b[i] * (double)b[i];
    }
    if (na <= 0 || nb <= 0) {
        return 0.f;
    }
    return static_cast<float>(dot / (std::sqrt(na) * std::sqrt(nb)));
}

static FaceMeta* FindMeta(int local_id) {
    for (int i = 0; i < s_meta_count; i++) {
        if (s_meta[i].local_id == local_id) {
            return &s_meta[i];
        }
    }
    return nullptr;
}

static void EnsureDisplayName(FaceMeta* m) {
    if (m->display_name[0] == '\0') {
        snprintf(m->display_name, sizeof(m->display_name), "#%d", m->local_id);
    }
}

static esp_err_t SaveMetaToNvs() {
    nvs_handle_t h;
    esp_err_t err = nvs_open(DEEP_DOG_FACE_RECOG_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, "meta_ver", kFaceMetaVer);
    if (err == ESP_OK) {
        err = nvs_set_i32(h, "next_id", s_next_id);
    }
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "meta", s_meta, sizeof(FaceMeta) * static_cast<size_t>(s_meta_count));
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "count", static_cast<uint8_t>(s_meta_count));
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static void LoadMetaFromNvs() {
    nvs_handle_t h;
    if (nvs_open(DEEP_DOG_FACE_RECOG_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t ver = 0;
    (void)nvs_get_u8(h, "meta_ver", &ver);
    int32_t next = 1;
    if (nvs_get_i32(h, "next_id", &next) == ESP_OK && next > 0) {
        s_next_id = next;
    }
    uint8_t count = 0;
    if (nvs_get_u8(h, "count", &count) == ESP_OK && count > 0) {
        if (count > DEEP_DOG_FACE_RECOG_MAX) {
            count = DEEP_DOG_FACE_RECOG_MAX;
        }
        size_t need = sizeof(FaceMeta) * count;
        size_t sz = need;
        if (ver == kFaceMetaVer && nvs_get_blob(h, "meta", s_meta, &sz) == ESP_OK && sz == need) {
            s_meta_count = count;
        } else {
            ESP_LOGW(TAG, "face meta ver/size mismatch (ver=%u), rebuild from facedb", (unsigned)ver);
            s_meta_count = 0;
            memset(s_meta, 0, sizeof(s_meta));
        }
    }
    nvs_close(h);
    for (int i = 0; i < s_meta_count; i++) {
        EnsureDisplayName(&s_meta[i]);
        if (s_meta[i].local_id >= s_next_id) {
            s_next_id = s_meta[i].local_id + 1;
        }
    }
}

static FaceMeta* UpsertMeta(int local_id) {
    FaceMeta* m = FindMeta(local_id);
    if (m) {
        m->updated_at = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        EnsureDisplayName(m);
        return m;
    }
    if (s_meta_count >= DEEP_DOG_FACE_RECOG_MAX) {
        return nullptr;
    }
    m = &s_meta[s_meta_count++];
    memset(m, 0, sizeof(*m));
    m->local_id = local_id;
    m->updated_at = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    EnsureDisplayName(m);
    if (local_id >= s_next_id) {
        s_next_id = local_id + 1;
    }
    return m;
}

static bool MountFacedb() {
    if (s_fs_mounted) {
        return true;
    }
    const esp_vfs_fat_mount_config_t conf = {
        .format_if_mount_failed = true,
        .max_files = 4,
        .allocation_unit_size = 4096,
        .disk_status_check_enable = false,
    };
    esp_err_t err =
        esp_vfs_fat_spiflash_mount_rw_wl(DEEP_DOG_FACE_RECOG_DB_MOUNT, "facedb", &conf, &s_wl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount facedb failed: %s", esp_err_to_name(err));
        return false;
    }
    s_fs_mounted = true;
    ESP_LOGI(TAG, "facedb mounted at %s", DEEP_DOG_FACE_RECOG_DB_MOUNT);
    return true;
}

static void UnmountFacedb() {
    if (!s_fs_mounted) {
        return;
    }
    esp_vfs_fat_spiflash_unmount_rw_wl(DEEP_DOG_FACE_RECOG_DB_MOUNT, s_wl);
    s_wl = WL_INVALID_HANDLE;
    s_fs_mounted = false;
}

static void ClearSession() {
    if (s_session_feat) {
        heap_caps_free(s_session_feat);
        s_session_feat = nullptr;
    }
    s_session_feat_len = 0;
    s_session_id = 0;
    s_session_us = 0;
}

static void UpdateSession(int local_id, const float* feat, int feat_len) {
    if (!feat || feat_len <= 0) {
        return;
    }
    if (!s_session_feat || s_session_feat_len != feat_len) {
        if (s_session_feat) {
            heap_caps_free(s_session_feat);
        }
        s_session_feat = (float*)heap_caps_malloc(sizeof(float) * (size_t)feat_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_session_feat) {
            s_session_feat = (float*)heap_caps_malloc(sizeof(float) * (size_t)feat_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        s_session_feat_len = s_session_feat ? feat_len : 0;
    }
    if (!s_session_feat) {
        return;
    }
    memcpy(s_session_feat, feat, sizeof(float) * (size_t)feat_len);
    s_session_id = local_id;
    s_session_us = esp_timer_get_time();
}

static std::list<dl::detect::result_t> SingleDetectList(const std::list<dl::detect::result_t>& raw, int idx) {
    std::list<dl::detect::result_t> one;
    int i = 0;
    for (const auto& r : raw) {
        if (i == idx) {
            one.push_back(r);
            break;
        }
        i++;
    }
    return one;
}

/** 按 score 降序返回 raw 下标（仅含 keypoints 足够的） */
static void RankDetectIndices(const std::list<dl::detect::result_t>& raw, std::vector<int>* out) {
    out->clear();
    std::vector<std::pair<float, int>> scored;
    int i = 0;
    for (const auto& r : raw) {
        if (r.keypoint.size() >= 10) {
            scored.push_back({r.score, i});
        }
        i++;
    }
    std::sort(scored.begin(), scored.end(),
              [](const std::pair<float, int>& a, const std::pair<float, int>& b) { return a.first > b.first; });
    for (const auto& p : scored) {
        out->push_back(p.second);
        if ((int)out->size() >= DEEP_DOG_FACE_RECOG_MULTI_MAX) {
            break;
        }
    }
}

/** boxes 与 raw 同序时直接用下标；否则按 score 最近匹配尚未占用的 box */
static int MatchBoxIndex(const std::vector<DeepDogFaceBox>& boxes, const dl::detect::result_t& r, int raw_i,
                         const std::vector<uint8_t>& used) {
    if (raw_i >= 0 && raw_i < (int)boxes.size() && !used[(size_t)raw_i]) {
        if (std::fabs(boxes[(size_t)raw_i].score - r.score) < 0.05f) {
            return raw_i;
        }
    }
    int best = -1;
    float best_d = 1e9f;
    for (size_t i = 0; i < boxes.size(); i++) {
        if (used[i]) {
            continue;
        }
        const float d = std::fabs(boxes[i].score - r.score);
        if (d < best_d) {
            best_d = d;
            best = (int)i;
        }
    }
    return best;
}

static void ApplyMetaToBox(DeepDogFaceBox* box, int local_id, DeepDogFaceRecognizeSource src) {
    box->local_id = local_id;
    box->recognize_source = src;
    FaceMeta* m = FindMeta(local_id);
    if (m) {
        EnsureDisplayName(m);
        strncpy(box->display_name, m->display_name, sizeof(box->display_name) - 1);
        box->display_name[sizeof(box->display_name) - 1] = '\0';
    } else {
        snprintf(box->display_name, sizeof(box->display_name), "#%d", local_id);
    }
}

static void RecognizeOneFace(const dl::image::img_t& img, const std::list<dl::detect::result_t>& one,
                             DeepDogFaceBox* box, bool update_session) {
    if (!box || one.empty()) {
        return;
    }
    HumanFaceFeat* feat_model = s_recog->get_feat_model();
    dl::TensorBase* feat_t = nullptr;
    int feat_len = feat_model ? feat_model->get_feat_len() : 0;
    if (feat_model) {
        feat_t = feat_model->run(img, one.front().keypoint);
    }
    const int64_t now = esp_timer_get_time();

    if (update_session && feat_t && feat_len > 0 && s_session_feat && s_session_id > 0 &&
        (now - s_session_us) < (int64_t)DEEP_DOG_FACE_RECOG_SESSION_MS * 1000) {
        if (feat_len == s_session_feat_len) {
            auto* fptr = feat_t->get_element_ptr<float>();
            if (fptr) {
                const float sim = CosineSim(fptr, s_session_feat, feat_len);
                if (sim >= DEEP_DOG_FACE_RECOG_SIM_THR) {
                    if (!FindMeta(s_session_id)) {
                        UpsertMeta(s_session_id);
                    }
                    ApplyMetaToBox(box, s_session_id, DeepDogFaceRecognizeSource::Session);
                    UpdateSession(s_session_id, fptr, feat_len);
                    return;
                }
            }
        }
    }

    auto hits = s_recog->recognize(img, one);
    if (!hits.empty() && hits[0].similarity >= DEEP_DOG_FACE_RECOG_SIM_THR) {
        const int id = static_cast<int>(hits[0].id);
        if (!FindMeta(id)) {
            UpsertMeta(id);
            (void)SaveMetaToNvs();
        }
        ApplyMetaToBox(box, id, DeepDogFaceRecognizeSource::Nvs);
        if (update_session && feat_t && feat_len > 0) {
            auto* fptr = feat_t->get_element_ptr<float>();
            if (fptr) {
                UpdateSession(id, fptr, feat_len);
            } else {
                s_session_id = id;
                s_session_us = now;
            }
        }
        return;
    }

    if (s_recog->get_num_feats() >= DEEP_DOG_FACE_RECOG_MAX) {
        ESP_LOGW(TAG, "gallery full (%d), skip enroll", DEEP_DOG_FACE_RECOG_MAX);
        return;
    }
    if (s_recog->enroll(img, one) != ESP_OK) {
        ESP_LOGW(TAG, "enroll failed");
        return;
    }
    int new_id = s_next_id;
    auto after = s_recog->recognize(img, one);
    if (!after.empty()) {
        new_id = static_cast<int>(after[0].id);
    }
    FaceMeta* m = UpsertMeta(new_id);
    if (!m) {
        return;
    }
    EnsureDisplayName(m);
    (void)SaveMetaToNvs();
    ApplyMetaToBox(box, new_id, DeepDogFaceRecognizeSource::Enrolled);
    if (update_session && feat_t && feat_len > 0) {
        auto* fptr = feat_t->get_element_ptr<float>();
        if (fptr) {
            UpdateSession(new_id, fptr, feat_len);
        } else {
            s_session_id = new_id;
            s_session_us = now;
        }
    }
    ESP_LOGI(TAG, "enrolled local_id=%d name=%s feats=%d", new_id, box->display_name, s_recog->get_num_feats());
}

void DeepDogFaceRecognizeProcess(const dl::image::img_t& img, const std::list<dl::detect::result_t>& detect_raw,
                                 std::vector<DeepDogFaceBox>* boxes, DeepDogFaceSnapshot* snap_fields) {
    if (snap_fields) {
        snap_fields->primary_local_id = 0;
        snap_fields->primary_display_name[0] = '\0';
        snap_fields->primary_source = DeepDogFaceRecognizeSource::None;
    }
    if (!boxes || boxes->empty() || detect_raw.empty()) {
        return;
    }
    if (!DeepDogFaceRecognizeReady() && !DeepDogFaceRecognizeInit()) {
        return;
    }

    std::vector<int> ranked;
    RankDetectIndices(detect_raw, &ranked);
    if (ranked.empty()) {
        return;
    }

    std::vector<uint8_t> used_box(boxes->size(), 0);
    DeepDogFaceBox* primary = nullptr;

    for (size_t k = 0; k < ranked.size(); k++) {
        const int raw_i = ranked[k];
        auto one = SingleDetectList(detect_raw, raw_i);
        if (one.empty()) {
            continue;
        }
        const int box_i = MatchBoxIndex(*boxes, one.front(), raw_i, used_box);
        if (box_i < 0 || box_i >= (int)boxes->size()) {
            continue;
        }
        used_box[(size_t)box_i] = 1;
        DeepDogFaceBox& box = (*boxes)[(size_t)box_i];
        RecognizeOneFace(img, one, &box, /*update_session=*/(k == 0));
        if (k == 0) {
            primary = &box;
        }
    }

    if (primary && primary->local_id > 0 && snap_fields) {
        snap_fields->primary_local_id = primary->local_id;
        strncpy(snap_fields->primary_display_name, primary->display_name,
                sizeof(snap_fields->primary_display_name) - 1);
        snap_fields->primary_display_name[sizeof(snap_fields->primary_display_name) - 1] = '\0';
        snap_fields->primary_source = primary->recognize_source;
    }
}

bool DeepDogFaceRecognizeInit() {
    if (s_ready) {
        return true;
    }
    LoadMetaFromNvs();
    if (!MountFacedb()) {
        return false;
    }
    s_recog = new HumanFaceRecognizer(DEEP_DOG_FACE_RECOG_DB_PATH, HumanFaceFeat::MFN_S8_V1, false);
    if (!s_recog) {
        ESP_LOGE(TAG, "HumanFaceRecognizer alloc failed");
        UnmountFacedb();
        return false;
    }
    const int n = s_recog->get_num_feats();
    for (int id = 1; id <= n && s_meta_count < DEEP_DOG_FACE_RECOG_MAX; id++) {
        if (!FindMeta(id)) {
            UpsertMeta(id);
        }
    }
    if (n > 0) {
        (void)SaveMetaToNvs();
    }
    s_ready = true;
    ESP_LOGI(TAG, "recognizer ready (feats=%d meta=%d next_id=%d thr=%.2f multi=%d)", n, s_meta_count, s_next_id,
             (double)DEEP_DOG_FACE_RECOG_SIM_THR, DEEP_DOG_FACE_RECOG_MULTI_MAX);
    return true;
}

void DeepDogFaceRecognizeDeinit() {
    ClearSession();
    delete s_recog;
    s_recog = nullptr;
    UnmountFacedb();
    s_ready = false;
}

bool DeepDogFaceRecognizeReady() {
    return s_ready && s_recog != nullptr;
}

bool DeepDogFaceRecognizeNeedsImmichName(int local_id) {
    if (local_id <= 0) {
        return false;
    }
    FaceMeta* m = FindMeta(local_id);
    if (!m) {
        return true;
    }
    return m->immich_person_id[0] == '\0' || IsPlaceholderName(local_id, m->display_name);
}

bool DeepDogFaceRecognizeBindImmichName(int local_id, const char* display_name, const char* immich_person_id) {
    if (local_id <= 0 || !display_name || !display_name[0]) {
        return false;
    }
    FaceMeta* m = UpsertMeta(local_id);
    if (!m) {
        return false;
    }
    strncpy(m->display_name, display_name, sizeof(m->display_name) - 1);
    m->display_name[sizeof(m->display_name) - 1] = '\0';
    if (immich_person_id && immich_person_id[0]) {
        strncpy(m->immich_person_id, immich_person_id, sizeof(m->immich_person_id) - 1);
        m->immich_person_id[sizeof(m->immich_person_id) - 1] = '\0';
    }
    m->updated_at = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    return SaveMetaToNvs() == ESP_OK;
}

#else  // stub

bool DeepDogFaceRecognizeInit() {
    return true;
}
void DeepDogFaceRecognizeDeinit() {}
bool DeepDogFaceRecognizeReady() {
    return false;
}
bool DeepDogFaceRecognizeNeedsImmichName(int) {
    return false;
}
bool DeepDogFaceRecognizeBindImmichName(int, const char*, const char*) {
    return false;
}

#endif
