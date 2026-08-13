/**
 * Deep-dog 本地人脸数字 ID（S04）：HumanFaceRecognizer + facedb FAT + NVS 元数据。
 * S05 可写 Immich 真名到 meta；禁止在 httpd 回调内推理。
 */
#include "sdkconfig.h"

#include "face_ai_config.h"
#include "net/deep_dog_sntp.h"
#include "face_recognize.h"
#include "face_ai_types.h"
#include "face_greet.h"
#include "face_persist.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
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

/** meta_ver=4：+last_seen_at（LRU / 上次见面） */
static constexpr uint8_t kFaceMetaVer = 4;
static constexpr uint8_t kFaceMetaVerLegacy = 2;
static constexpr uint8_t kFaceMetaVer3 = 3;

struct FaceMetaV3 {
    int local_id = 0;
    int canonical_id = 0;
    char display_name[32] = {};
    char immich_person_id[40] = {};
    char immich_asset_id[48] = {};
    uint32_t updated_at = 0;
    uint8_t name_pending = 0;
    uint8_t reserved[3] = {};
};

struct FaceMetaV2 {
    int local_id = 0;
    char display_name[32] = {};
    uint32_t updated_at = 0;
    char immich_person_id[40] = {};
};

struct FaceMeta {
    int local_id = 0;
    int canonical_id = 0;
    char display_name[32] = {};
    char immich_person_id[40] = {};
    char immich_asset_id[48] = {};
    uint32_t updated_at = 0;
    uint32_t last_seen_at = 0;
    uint8_t name_pending = 0;
    uint8_t reserved[3] = {};
};

static std::function<void()> s_registry_changed_cb;

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

static FaceMeta* FindMeta(int local_id);

/** 会话：最近一次成功识别的 feat 向量副本 */
static float* s_session_feat = nullptr;
static int s_session_feat_len = 0;
static int s_session_id = 0;
static int64_t s_session_us = 0;

static esp_err_t SaveMetaToNvsDirect();

static void PersistMetaAsync() {
    if (DeepDogFacePersistIsReady()) {
        DeepDogFacePersistFlushAsync();
        return;
    }
    (void)SaveMetaToNvsDirect();
}

static bool PersistMetaSync() {
    if (DeepDogFacePersistIsReady()) {
        return DeepDogFacePersistFlushSync();
    }
    return SaveMetaToNvsDirect() == ESP_OK;
}

static uint32_t NowUnixSec() {
    return DeepDogNowUnixSec();
}

/** 契约：face/registry 的 updated_at / last_seen_at 均为 Unix 秒（≥1e9 表 SNTP 已同步） */
static bool IsPlausibleUnixSec(uint32_t t) {
    return t >= 1000000000u;
}

static void SanitizeMetaTimestamps() {
    bool dirty = false;
    for (int i = 0; i < s_meta_count; i++) {
        FaceMeta& m = s_meta[i];
        if (m.updated_at > 0 && !IsPlausibleUnixSec(m.updated_at)) {
            m.updated_at = 0;
            dirty = true;
        }
        if (m.last_seen_at > 0 && !IsPlausibleUnixSec(m.last_seen_at)) {
            m.last_seen_at = 0;
            dirty = true;
        }
    }
    if (dirty) {
        ESP_LOGW(TAG, "sanitized meta timestamps (legacy boot-ms -> 0)");
        (void)SaveMetaToNvsDirect();
    }
}

static void TouchLastSeen(int local_id) {
    if (local_id <= 0) {
        return;
    }
    const uint32_t now = NowUnixSec();
    if (!IsPlausibleUnixSec(now)) {
        ESP_LOGD(TAG, "TouchLastSeen skipped (clock not synced)");
        return;
    }
    FaceMeta* m = FindMeta(local_id);
    if (m) {
        m->last_seen_at = now;
    }
    const int cid = DeepDogFaceRecognizeResolveCanonicalId(local_id);
    if (cid > 0 && cid != local_id) {
        FaceMeta* c = FindMeta(cid);
        if (c) {
            c->last_seen_at = now;
        }
    }
}

static uint32_t MetaLastSeenForLru(const FaceMeta& m) {
    if (m.last_seen_at > 0) {
        return m.last_seen_at;
    }
    return 0;
}

static bool EvictOldestFeatSlot() {
    if (s_meta_count <= 0) {
        return false;
    }
    int victim_id = 0;
    uint32_t oldest = UINT32_MAX;
    int victim_score = -1;
    for (int i = 0; i < s_meta_count; i++) {
        const FaceMeta& m = s_meta[i];
        const uint32_t seen = MetaLastSeenForLru(m);
        int score = 0;
        if (IsPlaceholderName(m.local_id, m.display_name)) {
            score += 0;
        } else {
            score += 2;
        }
        if (m.immich_person_id[0]) {
            score += 4;
        }
        if (victim_id <= 0 || seen < oldest || (seen == oldest && score < victim_score)) {
            oldest = seen;
            victim_score = score;
            victim_id = m.local_id;
        }
    }
    if (victim_id <= 0) {
        return false;
    }
    ESP_LOGI(TAG, "LRU evict local_id=%d last_seen=%u", victim_id, oldest);
    return DeepDogFaceRecognizeDeleteOne(victim_id);
}

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

static void NotifyRegistryChanged() {
    if (s_registry_changed_cb) {
        s_registry_changed_cb();
    }
}

static bool IsCanonicalMeta(const FaceMeta* m) {
    return m && (m->canonical_id <= 0 || m->canonical_id == m->local_id);
}

static int FindCanonicalByPersonId(const char* person_id, int exclude_local_id) {
    if (!person_id || !person_id[0]) {
        return 0;
    }
    for (int i = 0; i < s_meta_count; i++) {
        const FaceMeta& m = s_meta[i];
        if (m.local_id == exclude_local_id) {
            continue;
        }
        if (!IsCanonicalMeta(&m)) {
            continue;
        }
        if (m.immich_person_id[0] && strcmp(m.immich_person_id, person_id) == 0) {
            return m.local_id;
        }
    }
    return 0;
}

static void MetaToEntry(const FaceMeta& m, DeepDogFaceEnrolledEntry* out) {
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->local_id = m.local_id;
    out->canonical_id = m.canonical_id;
    strncpy(out->display_name, m.display_name, sizeof(out->display_name) - 1);
    strncpy(out->immich_person_id, m.immich_person_id, sizeof(out->immich_person_id) - 1);
    strncpy(out->immich_asset_id, m.immich_asset_id, sizeof(out->immich_asset_id) - 1);
    out->updated_at = m.updated_at;
    out->last_seen_at = m.last_seen_at;
    out->name_pending = m.name_pending != 0;
    for (int i = 0; i < s_meta_count && out->alias_count < DEEP_DOG_FACE_REGISTRY_MAX_ALIASES; i++) {
        const FaceMeta& a = s_meta[i];
        if (a.local_id != m.local_id && a.canonical_id == m.local_id) {
            out->aliases[out->alias_count++] = a.local_id;
        }
    }
}

static void EnsureDisplayName(FaceMeta* m) {
    if (m->display_name[0] == '\0') {
        snprintf(m->display_name, sizeof(m->display_name), "#%d", m->local_id);
    }
}

static esp_err_t SaveMetaToNvsDirect() {
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

esp_err_t DeepDogFaceRecognizeSaveMetaToNvs() {
    return SaveMetaToNvsDirect();
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
        } else if (ver == kFaceMetaVer && sz == sizeof(FaceMetaV3) * count) {
            std::vector<FaceMetaV3> legacy(count);
            if (nvs_get_blob(h, "meta", legacy.data(), &sz) == ESP_OK) {
                s_meta_count = 0;
                for (int i = 0; i < count && s_meta_count < DEEP_DOG_FACE_RECOG_MAX; i++) {
                    FaceMeta* m = &s_meta[s_meta_count++];
                    memset(m, 0, sizeof(*m));
                    m->local_id = legacy[(size_t)i].local_id;
                    m->canonical_id = legacy[(size_t)i].canonical_id;
                    strncpy(m->display_name, legacy[(size_t)i].display_name, sizeof(m->display_name) - 1);
                    m->updated_at = legacy[(size_t)i].updated_at;
                    m->last_seen_at = 0;
                    strncpy(m->immich_person_id, legacy[(size_t)i].immich_person_id, sizeof(m->immich_person_id) - 1);
                    strncpy(m->immich_asset_id, legacy[(size_t)i].immich_asset_id, sizeof(m->immich_asset_id) - 1);
                    m->name_pending = legacy[(size_t)i].name_pending;
                }
                ESP_LOGI(TAG, "migrated meta ver4(v3-layout)->4 count=%d", s_meta_count);
                (void)SaveMetaToNvsDirect();
            }
        } else if (ver == kFaceMetaVer3 && sz == sizeof(FaceMetaV3) * count) {
            std::vector<FaceMetaV3> legacy(count);
            if (nvs_get_blob(h, "meta", legacy.data(), &sz) == ESP_OK) {
                s_meta_count = 0;
                for (int i = 0; i < count && s_meta_count < DEEP_DOG_FACE_RECOG_MAX; i++) {
                    FaceMeta* m = &s_meta[s_meta_count++];
                    memset(m, 0, sizeof(*m));
                    m->local_id = legacy[(size_t)i].local_id;
                    m->canonical_id = legacy[(size_t)i].canonical_id;
                    strncpy(m->display_name, legacy[(size_t)i].display_name, sizeof(m->display_name) - 1);
                    m->updated_at = legacy[(size_t)i].updated_at;
                    m->last_seen_at = 0;
                    strncpy(m->immich_person_id, legacy[(size_t)i].immich_person_id, sizeof(m->immich_person_id) - 1);
                    strncpy(m->immich_asset_id, legacy[(size_t)i].immich_asset_id, sizeof(m->immich_asset_id) - 1);
                    m->name_pending = legacy[(size_t)i].name_pending;
                }
                ESP_LOGI(TAG, "migrated meta ver3->4 count=%d", s_meta_count);
                (void)SaveMetaToNvsDirect();
            }
        } else if (ver == kFaceMetaVerLegacy && sz == sizeof(FaceMetaV2) * count) {
            std::vector<FaceMetaV2> legacy(count);
            if (nvs_get_blob(h, "meta", legacy.data(), &sz) == ESP_OK) {
                s_meta_count = 0;
                for (int i = 0; i < count && s_meta_count < DEEP_DOG_FACE_RECOG_MAX; i++) {
                    FaceMeta* m = &s_meta[s_meta_count++];
                    memset(m, 0, sizeof(*m));
                    m->local_id = legacy[(size_t)i].local_id;
                    strncpy(m->display_name, legacy[(size_t)i].display_name, sizeof(m->display_name) - 1);
                    m->updated_at = legacy[(size_t)i].updated_at;
                    m->last_seen_at = 0;
                    strncpy(m->immich_person_id, legacy[(size_t)i].immich_person_id, sizeof(m->immich_person_id) - 1);
                }
                ESP_LOGI(TAG, "migrated meta ver2->3 count=%d", s_meta_count);
                (void)SaveMetaToNvsDirect();
            }
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
    SanitizeMetaTimestamps();
}

static FaceMeta* UpsertMeta(int local_id) {
    FaceMeta* m = FindMeta(local_id);
    if (m) {
        m->updated_at = NowUnixSec();
        EnsureDisplayName(m);
        return m;
    }
    if (s_meta_count >= DEEP_DOG_FACE_RECOG_MAX) {
        return nullptr;
    }
    m = &s_meta[s_meta_count++];
    memset(m, 0, sizeof(*m));
    m->local_id = local_id;
    m->updated_at = NowUnixSec();
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

static void ApplyMetaToBox(DeepDogFaceBox* box, int raw_local_id, DeepDogFaceRecognizeSource src) {
    const int cid = DeepDogFaceRecognizeResolveCanonicalId(raw_local_id);
    box->local_id = cid;
    box->recognize_source = src;
    FaceMeta* m = FindMeta(cid);
    if (!m && cid != raw_local_id) {
        m = FindMeta(raw_local_id);
    }
    if (m) {
        EnsureDisplayName(m);
        strncpy(box->display_name, m->display_name, sizeof(box->display_name) - 1);
        box->display_name[sizeof(box->display_name) - 1] = '\0';
    } else {
        snprintf(box->display_name, sizeof(box->display_name), "#%d", cid);
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
                    uint32_t last_seen = 0;
                    const char* pid = nullptr;
                    if (FaceMeta* m = FindMeta(s_session_id)) {
                        last_seen = m->last_seen_at;
                        if (m->immich_person_id[0]) {
                            pid = m->immich_person_id;
                        }
                    }
                    (void)DeepDogFaceGreetMaybeFromRecognition(s_session_id, box->display_name, pid, last_seen);
                    TouchLastSeen(s_session_id);
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
            PersistMetaAsync();
        }
        ApplyMetaToBox(box, id, DeepDogFaceRecognizeSource::Nvs);
        uint32_t last_seen = 0;
        const char* pid = nullptr;
        if (FaceMeta* m = FindMeta(id)) {
            last_seen = m->last_seen_at;
            if (m->immich_person_id[0]) {
                pid = m->immich_person_id;
            }
        }
        (void)DeepDogFaceGreetMaybeFromRecognition(id, box->display_name, pid, last_seen);
        TouchLastSeen(id);
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
        if (!EvictOldestFeatSlot()) {
            ESP_LOGW(TAG, "gallery full (%d), evict failed", DEEP_DOG_FACE_RECOG_MAX);
            return;
        }
    }
    if (box->score > 0.f && box->score < DEEP_DOG_FACE_RECOG_MIN_ENROLL_SCORE) {
        ESP_LOGD(TAG, "enroll skipped low score=%.2f", box->score);
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
    TouchLastSeen(new_id);
    PersistMetaAsync();
    ApplyMetaToBox(box, new_id, DeepDogFaceRecognizeSource::Enrolled);
    NotifyRegistryChanged();
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
        (void)SaveMetaToNvsDirect();
    }
    s_ready = true;
    ESP_LOGI(TAG, "recognizer ready (feats=%d meta=%d next_id=%d thr=%.2f multi=%d)", n, s_meta_count, s_next_id,
             (double)DEEP_DOG_FACE_RECOG_SIM_THR, DEEP_DOG_FACE_RECOG_MULTI_MAX);
    if (n > 0 || s_meta_count > 0) {
        NotifyRegistryChanged();
    }
    return true;
}

void DeepDogFaceRecognizeDeinit() {
    ClearSession();
    delete s_recog;
    s_recog = nullptr;
    UnmountFacedb();
    s_ready = false;
}

bool DeepDogFaceRecognizeClearAll() {
    ClearSession();
    if (!s_recog) {
        s_meta_count = 0;
        memset(s_meta, 0, sizeof(s_meta));
        s_next_id = 1;
        (void)PersistMetaSync();
        ESP_LOGW(TAG, "clear_db: recognizer not ready, meta reset only");
        return true;
    }
    esp_err_t err = s_recog->clear_all_feats();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "clear_all_feats failed: %s", esp_err_to_name(err));
        return false;
    }
    s_meta_count = 0;
    memset(s_meta, 0, sizeof(s_meta));
    s_next_id = 1;
    if (!PersistMetaSync()) {
        ESP_LOGW(TAG, "clear_db: feats cleared but NVS meta save failed");
    }
    ESP_LOGI(TAG, "clear_db ok (feats=%d)", s_recog->get_num_feats());
    NotifyRegistryChanged();
    return true;
}

int DeepDogFaceRecognizeResolveCanonicalId(int local_id) {
    if (local_id <= 0) {
        return 0;
    }
    FaceMeta* m = FindMeta(local_id);
    if (!m) {
        return local_id;
    }
    if (m->canonical_id <= 0 || m->canonical_id == local_id) {
        return local_id;
    }
    return m->canonical_id;
}

bool DeepDogFaceRecognizeIsCanonical(int local_id) {
    FaceMeta* m = FindMeta(local_id);
    return m && IsCanonicalMeta(m);
}

bool DeepDogFaceRecognizeBindImmichAsset(int local_id, const char* asset_id) {
    if (local_id <= 0 || !asset_id || !asset_id[0]) {
        return false;
    }
    FaceMeta* m = UpsertMeta(local_id);
    if (!m) {
        return false;
    }
    strncpy(m->immich_asset_id, asset_id, sizeof(m->immich_asset_id) - 1);
    m->immich_asset_id[sizeof(m->immich_asset_id) - 1] = '\0';
    m->name_pending = 1;
    m->updated_at = NowUnixSec();
    const bool ok = PersistMetaSync();
    if (ok) {
        NotifyRegistryChanged();
    }
    return ok;
}

bool DeepDogFaceRecognizeRename(int local_id, const char* display_name) {
    const int cid = DeepDogFaceRecognizeResolveCanonicalId(local_id);
    if (cid <= 0 || !display_name || !display_name[0]) {
        return false;
    }
    FaceMeta* m = UpsertMeta(cid);
    if (!m) {
        return false;
    }
    strncpy(m->display_name, display_name, sizeof(m->display_name) - 1);
    m->display_name[sizeof(m->display_name) - 1] = '\0';
    m->updated_at = NowUnixSec();
    const bool ok = PersistMetaSync();
    if (ok) {
        NotifyRegistryChanged();
    }
    return ok;
}

bool DeepDogFaceRecognizeDeleteOne(int local_id) {
    if (local_id <= 0) {
        return false;
    }
    if (s_recog) {
        (void)s_recog->delete_feat(static_cast<uint16_t>(local_id));
    }
    int write = 0;
    for (int i = 0; i < s_meta_count; i++) {
        if (s_meta[i].local_id == local_id) {
            continue;
        }
        if (write != i) {
            s_meta[write] = s_meta[i];
        }
        write++;
    }
    if (write < s_meta_count) {
        s_meta_count = write;
    }
    for (int i = 0; i < s_meta_count; i++) {
        if (s_meta[i].canonical_id == local_id) {
            s_meta[i].canonical_id = 0;
        }
    }
    if (s_session_id == local_id) {
        ClearSession();
    }
    const bool ok = PersistMetaSync();
    NotifyRegistryChanged();
    return ok;
}

bool DeepDogFaceRecognizeMergeAlias(int source_local_id, int target_local_id) {
    if (source_local_id <= 0 || target_local_id <= 0 || source_local_id == target_local_id) {
        return false;
    }
    FaceMeta* src = FindMeta(source_local_id);
    if (!src) {
        FaceMeta* created = UpsertMeta(source_local_id);
        if (!created) {
            return false;
        }
        src = created;
    }
    FaceMeta* tgt = UpsertMeta(target_local_id);
    if (!tgt) {
        return false;
    }
    src->canonical_id = target_local_id;
    if (tgt->immich_person_id[0] && !src->immich_person_id[0]) {
        strncpy(src->immich_person_id, tgt->immich_person_id, sizeof(src->immich_person_id) - 1);
    }
    if (!IsPlaceholderName(target_local_id, tgt->display_name) && IsPlaceholderName(source_local_id, src->display_name)) {
        strncpy(src->display_name, tgt->display_name, sizeof(src->display_name) - 1);
    }
    src->updated_at = NowUnixSec();
    const bool ok = PersistMetaSync();
    if (ok) {
        NotifyRegistryChanged();
    }
    ESP_LOGI(TAG, "merge alias %d -> %d", source_local_id, target_local_id);
    return ok;
}

int DeepDogFaceRecognizeListCanonical(std::vector<DeepDogFaceEnrolledEntry>* out) {
    if (!out) {
        return 0;
    }
    out->clear();
    for (int i = 0; i < s_meta_count; i++) {
        if (!IsCanonicalMeta(&s_meta[i])) {
            continue;
        }
        DeepDogFaceEnrolledEntry e{};
        MetaToEntry(s_meta[i], &e);
        out->push_back(e);
    }
    return static_cast<int>(out->size());
}

static void JsonEscape(const char* in, char* out, size_t out_sz) {
    if (!out || out_sz == 0) {
        return;
    }
    out[0] = '\0';
    if (!in) {
        return;
    }
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 2 < out_sz; i++) {
        const char c = in[i];
        if (c == '"' || c == '\\') {
            if (j + 2 >= out_sz) {
                break;
            }
            out[j++] = '\\';
        }
        out[j++] = c;
    }
    out[j] = '\0';
}

size_t DeepDogFaceRecognizeFormatRegistryJson(char* buf, size_t buf_size) {
    if (!buf || buf_size < 32) {
        return 0;
    }
    std::vector<DeepDogFaceEnrolledEntry> entries;
    (void)DeepDogFaceRecognizeListCanonical(&entries);
    const int feat_count = s_recog ? s_recog->get_num_feats() : 0;
    int n = snprintf(buf, buf_size,
                     "{\"version\":1,\"count\":%d,\"feat_count\":%d,\"max_count\":%d,\"entries\":[",
                     static_cast<int>(entries.size()), feat_count, DEEP_DOG_FACE_RECOG_MAX);
    if (n < 0 || static_cast<size_t>(n) >= buf_size) {
        return 0;
    }
    size_t off = static_cast<size_t>(n);
    char esc[96];
    for (size_t i = 0; i < entries.size(); i++) {
        const auto& e = entries[i];
        JsonEscape(e.display_name, esc, sizeof(esc));
        char esc_pid[48] = {};
        char esc_aid[56] = {};
        JsonEscape(e.immich_person_id, esc_pid, sizeof(esc_pid));
        JsonEscape(e.immich_asset_id, esc_aid, sizeof(esc_aid));
        char aliases[64] = {};
        aliases[0] = '\0';
        if (e.alias_count > 0) {
            int ap = snprintf(aliases, sizeof(aliases), ",\"aliases\":[");
            for (int a = 0; a < e.alias_count && ap > 0 && static_cast<size_t>(ap) < sizeof(aliases) - 8; a++) {
                ap += snprintf(aliases + ap, sizeof(aliases) - static_cast<size_t>(ap), "%s%d", a ? "," : "", e.aliases[a]);
            }
            if (ap > 0 && static_cast<size_t>(ap) < sizeof(aliases) - 2) {
                snprintf(aliases + ap, sizeof(aliases) - static_cast<size_t>(ap), "]");
            }
        }
        const int w = e.last_seen_at > 0
                          ? snprintf(buf + off, buf_size - off,
                                     "%s{\"local_id\":%d,\"display_name\":\"%s\",\"immich_person_id\":\"%s\","
                                     "\"immich_asset_id\":\"%s\",\"name_pending\":%s,\"updated_at\":%u,"
                                     "\"last_seen_at\":%u%s}",
                                     i ? "," : "", e.local_id, esc, esc_pid, esc_aid,
                                     e.name_pending ? "true" : "false", (unsigned)e.updated_at,
                                     (unsigned)e.last_seen_at, e.alias_count > 0 ? aliases : "")
                          : snprintf(buf + off, buf_size - off,
                                     "%s{\"local_id\":%d,\"display_name\":\"%s\",\"immich_person_id\":\"%s\","
                                     "\"immich_asset_id\":\"%s\",\"name_pending\":%s,\"updated_at\":%u%s}",
                                     i ? "," : "", e.local_id, esc, esc_pid, esc_aid,
                                     e.name_pending ? "true" : "false", (unsigned)e.updated_at,
                                     e.alias_count > 0 ? aliases : "");
        if (w < 0 || static_cast<size_t>(w) >= buf_size - off) {
            break;
        }
        off += static_cast<size_t>(w);
    }
    if (off + 32 >= buf_size) {
        ESP_LOGW(TAG, "registry json buffer too small (entries=%u need>%u)", (unsigned)entries.size(),
                 (unsigned)(off + 32));
        return 0;
    }
    const time_t now = time(nullptr);
    const int64_t ts = (now > 1000000000) ? static_cast<int64_t>(now) : static_cast<int64_t>(esp_timer_get_time() / 1000000LL);
    const int t = snprintf(buf + off, buf_size - off, "],\"ts\":%ld}", static_cast<long>(ts));
    if (t < 0) {
        return off;
    }
    off += static_cast<size_t>(t);
    return off;
}

int DeepDogFaceRecognizeVisitPendingImmich(DeepDogFacePendingImmichVisitFn fn, void* ctx) {
    if (!fn) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < s_meta_count; i++) {
        const FaceMeta& m = s_meta[i];
        if (!m.immich_asset_id[0]) {
            continue;
        }
        if (!DeepDogFaceRecognizeNeedsImmichName(m.local_id)) {
            continue;
        }
        if (fn(m.local_id, m.immich_asset_id, ctx)) {
            n++;
        }
    }
    return n;
}

void DeepDogFaceRecognizeSetRegistryChangedCallback(std::function<void()> cb) {
    s_registry_changed_cb = std::move(cb);
}

int DeepDogFaceRecognizeGetFeatCount() {
    return s_recog ? s_recog->get_num_feats() : 0;
}

int DeepDogFaceRecognizeGetMaxFeats() {
    return DEEP_DOG_FACE_RECOG_MAX;
}

bool DeepDogFaceRecognizeReady() {
    return s_ready && s_recog != nullptr;
}

bool DeepDogFaceRecognizeNeedsImmichName(int local_id) {
    if (local_id <= 0) {
        return false;
    }
    const int cid = DeepDogFaceRecognizeResolveCanonicalId(local_id);
    FaceMeta* m = FindMeta(local_id);
    if (!m) {
        m = FindMeta(cid);
    }
    if (!m) {
        return true;
    }
    if (m->immich_asset_id[0] && (m->immich_person_id[0] == '\0' || IsPlaceholderName(cid, m->display_name))) {
        return true;
    }
    return m->immich_person_id[0] == '\0' || IsPlaceholderName(cid, m->display_name);
}

bool DeepDogFaceRecognizeBindImmichName(int local_id, const char* display_name, const char* immich_person_id) {
    if (local_id <= 0 || !display_name || !display_name[0]) {
        return false;
    }
    int cid = DeepDogFaceRecognizeResolveCanonicalId(local_id);
    if (immich_person_id && immich_person_id[0]) {
        const int existing = FindCanonicalByPersonId(immich_person_id, local_id);
        if (existing > 0 && existing != cid) {
            (void)DeepDogFaceRecognizeMergeAlias(local_id, existing);
            cid = existing;
        }
    }
    FaceMeta* m = UpsertMeta(cid);
    if (!m) {
        return false;
    }
    strncpy(m->display_name, display_name, sizeof(m->display_name) - 1);
    m->display_name[sizeof(m->display_name) - 1] = '\0';
    if (immich_person_id && immich_person_id[0]) {
        strncpy(m->immich_person_id, immich_person_id, sizeof(m->immich_person_id) - 1);
        m->immich_person_id[sizeof(m->immich_person_id) - 1] = '\0';
    }
    m->name_pending = 0;
    m->updated_at = NowUnixSec();
    FaceMeta* raw = FindMeta(local_id);
    if (raw && raw->local_id != cid) {
        raw->name_pending = 0;
    }
    const bool ok = PersistMetaSync();
    if (ok) {
        NotifyRegistryChanged();
    }
    return ok;
}

#else  // stub

bool DeepDogFaceRecognizeInit() {
    return true;
}
void DeepDogFaceRecognizeDeinit() {}
bool DeepDogFaceRecognizeClearAll() {
    return true;
}
bool DeepDogFaceRecognizeReady() {
    return false;
}
bool DeepDogFaceRecognizeNeedsImmichName(int) {
    return false;
}
bool DeepDogFaceRecognizeBindImmichName(int, const char*, const char*) {
    return false;
}
bool DeepDogFaceRecognizeBindImmichAsset(int, const char*) {
    return false;
}
bool DeepDogFaceRecognizeRename(int, const char*) {
    return false;
}
bool DeepDogFaceRecognizeDeleteOne(int) {
    return false;
}
bool DeepDogFaceRecognizeMergeAlias(int, int) {
    return false;
}
int DeepDogFaceRecognizeListCanonical(std::vector<DeepDogFaceEnrolledEntry>*) {
    return 0;
}
int DeepDogFaceRecognizeGetFeatCount() {
    return 0;
}
int DeepDogFaceRecognizeGetMaxFeats() {
    return DEEP_DOG_FACE_RECOG_MAX;
}
size_t DeepDogFaceRecognizeFormatRegistryJson(char*, size_t) {
    return 0;
}
int DeepDogFaceRecognizeVisitPendingImmich(DeepDogFacePendingImmichVisitFn, void*) {
    return 0;
}
void DeepDogFaceRecognizeSetRegistryChangedCallback(std::function<void()>) {}
int DeepDogFaceRecognizeResolveCanonicalId(int local_id) {
    return local_id;
}
bool DeepDogFaceRecognizeIsCanonical(int) {
    return false;
}

#endif
