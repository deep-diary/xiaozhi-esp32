#include "face_greet.h"

#include "application.h"
#include "device_state.h"
#include "face_ai_config.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <ctime>
#include <cstring>
#include <mutex>

#define TAG "dog_face_greet"

namespace {

constexpr const char* kNvsNs = "fdog_greet";
constexpr const char* kKeyEnabled = "g_en";
constexpr const char* kKeyGap = "g_gap";
constexpr int kDefaultGapSec = 10;
constexpr int kMinGapSec = 10;
constexpr int kMaxGapSec = 86400;
constexpr int kAbsentResetFrames = 8;
constexpr uint32_t kSpeakerGraceSec = 30;

std::mutex s_mu;
DeepDogFaceSpeaker s_speaker{};
std::function<void()> s_notify_cb;
bool s_greet_enabled = true;
int s_greet_gap_sec = kDefaultGapSec;
int s_last_greeted_id = 0;
int s_absent_streak = 0;
bool s_inited = false;

uint32_t NowUnixSec() {
    const time_t now = time(nullptr);
    if (now > 1000000000) {
        return static_cast<uint32_t>(now);
    }
    return static_cast<uint32_t>(esp_timer_get_time() / 1000000LL);
}

bool IsPlaceholderName(int local_id, const char* name) {
    if (!name || name[0] == '\0') {
        return true;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "#%d", local_id);
    return strcmp(name, buf) == 0;
}

void Notify() {
    if (s_notify_cb) {
        s_notify_cb();
    }
}

void LoadNvs() {
    nvs_handle_t h = 0;
    if (nvs_open(kNvsNs, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t en = 1;
    if (nvs_get_u8(h, kKeyEnabled, &en) == ESP_OK) {
        s_greet_enabled = en != 0;
    }
    int32_t gap = kDefaultGapSec;
    if (nvs_get_i32(h, kKeyGap, &gap) == ESP_OK) {
        if (gap >= kMinGapSec && gap <= kMaxGapSec) {
            s_greet_gap_sec = static_cast<int>(gap);
        }
    }
    nvs_close(h);
}

void SaveNvs() {
    nvs_handle_t h = 0;
    if (nvs_open(kNvsNs, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    (void)nvs_set_u8(h, kKeyEnabled, s_greet_enabled ? 1 : 0);
    (void)nvs_set_i32(h, kKeyGap, s_greet_gap_sec);
    (void)nvs_commit(h);
    nvs_close(h);
}

void TriggerWake() {
#if DEEP_DOG_FACE_AI_ENABLE
    Application::GetInstance().Schedule([]() {
        Application::GetInstance().WakeWordInvoke("face_greet");
    });
#endif
}

bool DeviceIdleEnough() {
#if DEEP_DOG_FACE_AI_ENABLE
    const auto st = Application::GetInstance().GetDeviceState();
    return st == kDeviceStateIdle || st == kDeviceStateUnknown;
#else
    return true;
#endif
}

void SetSpeakerLocked(int local_id, const char* name, const char* immich_person_id, DeepDogFaceGreetSource src,
                      bool greet_pending) {
    s_speaker = {};
    if (local_id > 0 && name && name[0]) {
        s_speaker.present = true;
        s_speaker.local_id = local_id;
        strncpy(s_speaker.display_name, name, sizeof(s_speaker.display_name) - 1);
        if (immich_person_id && immich_person_id[0]) {
            strncpy(s_speaker.immich_person_id, immich_person_id, sizeof(s_speaker.immich_person_id) - 1);
        }
        s_speaker.since = NowUnixSec();
        s_speaker.source = src;
        s_speaker.greet_pending = greet_pending;
    }
}

bool ShouldGreet(int local_id, const char* name, uint32_t last_seen_before, bool force) {
    if (!force && !s_greet_enabled) {
        return false;
    }
    if (local_id <= 0 || !DeepDogFaceGreetIsConfirmedName(local_id, name)) {
        return false;
    }
    if (!force && local_id == s_last_greeted_id) {
        return false;
    }
    if (!force && !DeviceIdleEnough()) {
        ESP_LOGW(TAG, "greet skipped (device busy)");
        return false;
    }
    if (force) {
        return true;
    }
    const uint32_t now = NowUnixSec();
    if (last_seen_before == 0) {
        return false;
    }
    const uint32_t gap = now >= last_seen_before ? now - last_seen_before : 0;
    if (gap < static_cast<uint32_t>(s_greet_gap_sec)) {
        return false;
    }
    return true;
}

}  // namespace

void DeepDogFaceGreetInit() {
    if (s_inited) {
        return;
    }
    LoadNvs();
    s_inited = true;
    ESP_LOGI(TAG, "init enabled=%d gap=%ds", s_greet_enabled ? 1 : 0, s_greet_gap_sec);
}

void DeepDogFaceGreetSetNotifyCallback(std::function<void()> cb) {
    std::lock_guard<std::mutex> lock(s_mu);
    s_notify_cb = std::move(cb);
}

bool DeepDogFaceGreetIsEnabled() {
    return s_greet_enabled;
}

int DeepDogFaceGreetGetGapSec() {
    return s_greet_gap_sec;
}

bool DeepDogFaceGreetSetConfig(bool enabled, int gap_sec) {
    if (gap_sec < kMinGapSec) {
        gap_sec = kMinGapSec;
    }
    if (gap_sec > kMaxGapSec) {
        gap_sec = kMaxGapSec;
    }
    {
        std::lock_guard<std::mutex> lock(s_mu);
        s_greet_enabled = enabled;
        s_greet_gap_sec = gap_sec;
    }
    SaveNvs();
    Notify();
    ESP_LOGI(TAG, "config enabled=%d gap=%d", enabled ? 1 : 0, gap_sec);
    return true;
}

void DeepDogFaceGreetSetSpeaker(int local_id, const char* name, const char* immich_person_id,
                                DeepDogFaceGreetSource src, bool greet_pending) {
    {
        std::lock_guard<std::mutex> lock(s_mu);
        SetSpeakerLocked(local_id, name, immich_person_id, src, greet_pending);
    }
    Notify();
}

void DeepDogFaceGreetClearSpeaker() {
    {
        std::lock_guard<std::mutex> lock(s_mu);
        s_speaker = {};
        s_last_greeted_id = 0;
        s_absent_streak = 0;
    }
    Notify();
    ESP_LOGI(TAG, "speaker cleared");
}

DeepDogFaceSpeaker DeepDogFaceGreetGetSpeaker() {
    std::lock_guard<std::mutex> lock(s_mu);
    return s_speaker;
}

bool DeepDogFaceGreetIsConfirmedName(int local_id, const char* name) {
    return !IsPlaceholderName(local_id, name);
}

bool DeepDogFaceGreetMaybeFromRecognition(int local_id, const char* display_name, const char* immich_person_id,
                                          uint32_t last_seen_before) {
    if (!DeepDogFaceGreetIsConfirmedName(local_id, display_name)) {
        return false;
    }
    bool do_greet = false;
    {
        std::lock_guard<std::mutex> lock(s_mu);
        do_greet = ShouldGreet(local_id, display_name, last_seen_before, false);
        SetSpeakerLocked(local_id, display_name, immich_person_id, DeepDogFaceGreetSource::Recognition, do_greet);
        if (do_greet) {
            s_last_greeted_id = local_id;
            s_absent_streak = 0;
        }
    }
    Notify();
    if (do_greet) {
        ESP_LOGI(TAG, "greet triggered id=%d name=%s last_seen=%u", local_id, display_name,
                 (unsigned)last_seen_before);
        TriggerWake();
    }
    return do_greet;
}

bool DeepDogFaceGreetSimulateAndWake(const char* name, int local_id) {
    if (!name || !name[0]) {
        ESP_LOGW(TAG, "simulate_greet missing name");
        return false;
    }
    const int lid = local_id > 0 ? local_id : 9000;
    {
        std::lock_guard<std::mutex> lock(s_mu);
        SetSpeakerLocked(lid, name, nullptr, DeepDogFaceGreetSource::Simulate, true);
        s_last_greeted_id = lid;
        s_absent_streak = 0;
    }
    Notify();
    ESP_LOGI(TAG, "simulate_greet name=%s local_id=%d ok=1", name, lid);
    TriggerWake();
    return true;
}

void DeepDogFaceGreetOnPrimaryFace(int local_id) {
    std::lock_guard<std::mutex> lock(s_mu);
    s_absent_streak = 0;
    (void)local_id;
}

void DeepDogFaceGreetOnNoFace() {
    std::lock_guard<std::mutex> lock(s_mu);
    s_absent_streak++;
    if (s_absent_streak < kAbsentResetFrames) {
        return;
    }
    if (s_speaker.present && s_speaker.since > 0) {
        const uint32_t now = NowUnixSec();
        if (now > s_speaker.since && (now - s_speaker.since) < kSpeakerGraceSec) {
            return;
        }
    }
    s_last_greeted_id = 0;
    s_absent_streak = 0;
}

const char* DeepDogFaceGreetSourceStr(DeepDogFaceGreetSource s) {
    switch (s) {
        case DeepDogFaceGreetSource::Recognition:
            return "recognition";
        case DeepDogFaceGreetSource::Simulate:
            return "simulate";
        default:
            return "none";
    }
}

std::string DeepDogFaceGreetFormatIdentityJson() {
    const DeepDogFaceSpeaker sp = DeepDogFaceGreetGetSpeaker();
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "present", sp.present && sp.local_id > 0);
    cJSON_AddNumberToObject(root, "local_id", sp.local_id);
    cJSON_AddStringToObject(root, "display_name", sp.display_name[0] ? sp.display_name : "");
    if (sp.immich_person_id[0]) {
        cJSON_AddStringToObject(root, "immich_person_id", sp.immich_person_id);
    }
    cJSON_AddNumberToObject(root, "recognized_at", static_cast<double>(sp.since));
    cJSON_AddStringToObject(root, "source", DeepDogFaceGreetSourceStr(sp.source));
    cJSON_AddBoolToObject(root, "greet_pending", sp.greet_pending);
    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return "{}";
    }
    std::string out(printed);
    cJSON_free(printed);
    return out;
}
