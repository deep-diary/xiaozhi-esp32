/**
 * Deep-dog 本地人脸数字 ID（S04）：HumanFaceRecognizer + facedb FAT + NVS 元数据。
 * 禁止 Immich / 禁止在 httpd 回调内推理。
 */
#include "sdkconfig.h"

#include "face_ai_config.h"
#include "face_recognize.h"
#include "face_ai_types.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
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

static HumanFaceRecognizer* s_recog = nullptr;
static wl_handle_t s_wl = WL_INVALID_HANDLE;
static bool s_fs_mounted = false;
static bool s_ready = false;

struct FaceMeta {
    int local_id = 0;
    char display_name[16] = {};
    uint32_t updated_at = 0;
    char immich_person_id[40] = {};
};

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
    err = nvs_set_i32(h, "next_id", s_next_id);
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
    int32_t next = 1;
    if (nvs_get_i32(h, "next_id", &next) == ESP_OK && next > 0) {
        s_next_id = next;
    }
    uint8_t count = 0;
    if (nvs_get_u8(h, "count", &count) == ESP_OK && count > 0) {
        if (count > DEEP_DOG_FACE_RECOG_MAX) {
            count = DEEP_DOG_FACE_RECOG_MAX;
        }
        size_t sz = sizeof(FaceMeta) * count;
        if (nvs_get_blob(h, "meta", s_meta, &sz) == ESP_OK) {
            s_meta_count = count;
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

static int PickBestIndex(const std::list<dl::detect::result_t>& raw) {
    int best_i = -1;
    float best_s = -1.f;
    int i = 0;
    for (const auto& r : raw) {
        if (r.score > best_s && r.keypoint.size() >= 10) {
            best_s = r.score;
            best_i = i;
        }
        i++;
    }
    return best_i;
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
    // 若 DB 已有特征但 NVS 空，补默认 meta
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
    ESP_LOGI(TAG, "recognizer ready (feats=%d meta=%d next_id=%d thr=%.2f)", n, s_meta_count, s_next_id,
             (double)DEEP_DOG_FACE_RECOG_SIM_THR);
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

    const int best_raw = PickBestIndex(detect_raw);
    if (best_raw < 0) {
        return;
    }
    // boxes 与 raw 可能因 min_box 过滤不完全对齐：按最高分 box 索引
    int best_box = 0;
    for (size_t i = 1; i < boxes->size(); i++) {
        if ((*boxes)[i].score > (*boxes)[best_box].score) {
            best_box = static_cast<int>(i);
        }
    }
    DeepDogFaceBox& primary = (*boxes)[static_cast<size_t>(best_box)];

    auto one = SingleDetectList(detect_raw, best_raw);
    if (one.empty()) {
        return;
    }

    HumanFaceFeat* feat_model = s_recog->get_feat_model();
    dl::TensorBase* feat_t = nullptr;
    int feat_len = feat_model ? feat_model->get_feat_len() : 0;
    if (feat_model) {
        feat_t = feat_model->run(img, one.front().keypoint);
    }

    const int64_t now = esp_timer_get_time();
    if (feat_t && feat_len > 0 && s_session_feat && s_session_id > 0 &&
        (now - s_session_us) < (int64_t)DEEP_DOG_FACE_RECOG_SESSION_MS * 1000) {
        if (feat_len == s_session_feat_len) {
            auto* fptr = feat_t->get_element_ptr<float>();
            if (fptr) {
                const float sim = CosineSim(fptr, s_session_feat, feat_len);
                if (sim >= DEEP_DOG_FACE_RECOG_SIM_THR) {
                    FaceMeta* m = FindMeta(s_session_id);
                    if (!m) {
                        m = UpsertMeta(s_session_id);
                    }
                    primary.local_id = s_session_id;
                    primary.recognize_source = DeepDogFaceRecognizeSource::Session;
                    if (m) {
                        strncpy(primary.display_name, m->display_name, sizeof(primary.display_name) - 1);
                    } else {
                        snprintf(primary.display_name, sizeof(primary.display_name), "#%d", s_session_id);
                    }
                    UpdateSession(s_session_id, fptr, feat_len);
                    if (snap_fields) {
                        snap_fields->primary_local_id = primary.local_id;
                        strncpy(snap_fields->primary_display_name, primary.display_name,
                                sizeof(snap_fields->primary_display_name) - 1);
                        snap_fields->primary_source = DeepDogFaceRecognizeSource::Session;
                    }
                    return;
                }
            }
        }
    }

    auto hits = s_recog->recognize(img, one);
    if (!hits.empty() && hits[0].similarity >= DEEP_DOG_FACE_RECOG_SIM_THR) {
        const int id = static_cast<int>(hits[0].id);
        FaceMeta* m = FindMeta(id);
        if (!m) {
            m = UpsertMeta(id);
            (void)SaveMetaToNvs();
        }
        primary.local_id = id;
        primary.recognize_source = DeepDogFaceRecognizeSource::Nvs;
        if (m) {
            strncpy(primary.display_name, m->display_name, sizeof(primary.display_name) - 1);
        } else {
            snprintf(primary.display_name, sizeof(primary.display_name), "#%d", id);
        }
        if (feat_t && feat_len > 0) {
            auto* fptr = feat_t->get_element_ptr<float>();
            if (fptr) {
                UpdateSession(id, fptr, feat_len);
            } else {
                s_session_id = id;
                s_session_us = now;
            }
        } else {
            s_session_id = id;
            s_session_us = now;
        }
        if (snap_fields) {
            snap_fields->primary_local_id = primary.local_id;
            strncpy(snap_fields->primary_display_name, primary.display_name,
                    sizeof(snap_fields->primary_display_name) - 1);
            snap_fields->primary_source = DeepDogFaceRecognizeSource::Nvs;
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
    if (m) {
        EnsureDisplayName(m);
        (void)SaveMetaToNvs();
        primary.local_id = new_id;
        primary.recognize_source = DeepDogFaceRecognizeSource::Enrolled;
        strncpy(primary.display_name, m->display_name, sizeof(primary.display_name) - 1);
        if (feat_t && feat_len > 0) {
            auto* fptr = feat_t->get_element_ptr<float>();
            if (fptr) {
                UpdateSession(new_id, fptr, feat_len);
            } else {
                s_session_id = new_id;
                s_session_us = now;
            }
        } else {
            s_session_id = new_id;
            s_session_us = now;
        }
        if (snap_fields) {
            snap_fields->primary_local_id = primary.local_id;
            strncpy(snap_fields->primary_display_name, primary.display_name,
                    sizeof(snap_fields->primary_display_name) - 1);
            snap_fields->primary_source = DeepDogFaceRecognizeSource::Enrolled;
        }
        ESP_LOGI(TAG, "enrolled local_id=%d name=%s feats=%d", new_id, primary.display_name, s_recog->get_num_feats());
    }
}

#else  // stub

bool DeepDogFaceRecognizeInit() {
    return true;
}
void DeepDogFaceRecognizeDeinit() {}
bool DeepDogFaceRecognizeReady() {
    return false;
}

#endif
