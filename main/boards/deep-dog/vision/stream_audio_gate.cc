/**
 * RTSP 推流期间暂停唤醒/语音，避免 AFE feed 与 vision_hub/face_ai 抢 CPU（V-S08）。
 */
#include "vision/stream_audio_gate.h"

#include "application.h"
#include "device_state.h"

#include <atomic>
#include <esp_log.h>

#define TAG "dog_stream_audio"

static std::atomic<bool> s_rtsp_active{false};

void DeepDogStreamAudioGateSetRtspActive(bool active) {
    const bool prev = s_rtsp_active.exchange(active, std::memory_order_acq_rel);
    if (prev == active) {
        return;
    }
    Application::GetInstance().Schedule([active]() {
        auto& app = Application::GetInstance();
        auto& audio = app.GetAudioService();
        if (active) {
            const DeviceState st = app.GetDeviceState();
            if (st == kDeviceStateListening || st == kDeviceStateSpeaking) {
                app.StopListening();
            }
            audio.EnableVoiceProcessing(false);
            audio.EnableWakeWordDetection(false);
            ESP_LOGI(TAG, "RTSP active: voice/AFE paused");
        } else {
            if (app.GetDeviceState() == kDeviceStateIdle) {
                audio.EnableWakeWordDetection(true);
            }
            ESP_LOGI(TAG, "RTSP inactive: wake word restored (idle)");
        }
    });
}

bool DeepDogStreamAudioGateIsVoicePaused() {
    return s_rtsp_active.load(std::memory_order_acquire);
}
