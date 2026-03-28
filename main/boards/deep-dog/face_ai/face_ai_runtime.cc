#include "sdkconfig.h"

#include "face_ai_bridge.h"
#include "face_ai_types.h"
#include "config.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#if DEEP_DOG_FACE_AI_ENABLE && defined(CONFIG_IDF_TARGET_ESP32S3)

extern bool DeepDogFaceDetectInit();
extern void DeepDogFaceDetectDeinit();
extern bool DeepDogFaceDetectRun(const uint8_t* rgb565, size_t len, uint16_t w, uint16_t h,
                                 std::vector<DeepDogFaceBox>* out);

#define TAG "dog_face_ai"

struct FaceFrameJob {
    uint16_t w = 0;
    uint16_t h = 0;
    uint8_t* data = nullptr;
    size_t len = 0;
};

static QueueHandle_t s_queue = nullptr;
static TaskHandle_t s_task = nullptr;
static std::atomic<bool> s_user_enabled{false};
static std::atomic<bool> s_runtime_started{false};
static int64_t s_last_submit_us = 0;
static std::mutex s_snap_mu;
static DeepDogFaceSnapshot s_snapshot;

static void FaceAiTask(void* /*arg*/) {
    FaceFrameJob job{};
    for (;;) {
        if (xQueueReceive(s_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (job.data == nullptr || job.len == 0) {
            continue;
        }
        std::vector<DeepDogFaceBox> boxes;
        if (s_user_enabled.load(std::memory_order_relaxed)) {
            (void)DeepDogFaceDetectRun(job.data, job.len, job.w, job.h, &boxes);
        }
        heap_caps_free(job.data);

        DeepDogFaceSnapshot snap{};
        snap.frame_w = job.w;
        snap.frame_h = job.h;
        snap.ts_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        snap.feature_enabled = s_user_enabled.load(std::memory_order_relaxed);
        snap.count = static_cast<int>(boxes.size());
        if (snap.count > 8) {
            snap.count = 8;
        }
        for (int i = 0; i < snap.count; i++) {
            snap.faces[i] = boxes[static_cast<size_t>(i)];
        }
        {
            std::lock_guard<std::mutex> lock(s_snap_mu);
            s_snapshot = snap;
        }
    }
}

bool DeepDogFaceAiRuntimeStart() {
    if (s_runtime_started.exchange(true)) {
        return true;
    }
    if (!DeepDogFaceDetectInit()) {
        s_runtime_started = false;
        return false;
    }
    s_queue = xQueueCreate(1, sizeof(FaceFrameJob));
    if (!s_queue) {
        DeepDogFaceDetectDeinit();
        s_runtime_started = false;
        return false;
    }
    if (xTaskCreate(FaceAiTask, "dog_face_ai", 8192, nullptr, 2, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = nullptr;
        DeepDogFaceDetectDeinit();
        s_runtime_started = false;
        return false;
    }
    ESP_LOGI(TAG, "runtime started (queue=1, interval>=%d ms)", DEEP_DOG_FACE_AI_MIN_INTERVAL_MS);
    return true;
}

void DeepDogFaceAiRuntimeStop() {
    if (!s_runtime_started.load()) {
        return;
    }
    if (s_task) {
        vTaskDelete(s_task);
        s_task = nullptr;
    }
    if (s_queue) {
        FaceFrameJob j{};
        while (xQueueReceive(s_queue, &j, 0) == pdTRUE) {
            if (j.data) {
                heap_caps_free(j.data);
            }
        }
        vQueueDelete(s_queue);
        s_queue = nullptr;
    }
    DeepDogFaceDetectDeinit();
    s_runtime_started = false;
}

void DeepDogFaceAiSetEnabled(bool on) {
    s_user_enabled.store(on, std::memory_order_relaxed);
    if (!on) {
        std::lock_guard<std::mutex> lock(s_snap_mu);
        s_snapshot.count = 0;
        s_snapshot.feature_enabled = false;
    } else {
        std::lock_guard<std::mutex> lock(s_snap_mu);
        s_snapshot.feature_enabled = true;
    }
}

bool DeepDogFaceAiIsEnabled() {
    return s_user_enabled.load(std::memory_order_relaxed);
}

void DeepDogFaceAiSubmitFrameIfDue(const uint8_t* rgb565, size_t len, uint16_t width, uint16_t height) {
    if (!s_runtime_started.load() || !rgb565 || width == 0 || height == 0) {
        return;
    }
    if (!s_user_enabled.load(std::memory_order_relaxed)) {
        return;
    }
    const int64_t now = esp_timer_get_time();
    if ((now - s_last_submit_us) < (int64_t)DEEP_DOG_FACE_AI_MIN_INTERVAL_MS * 1000) {
        return;
    }
    s_last_submit_us = now;

    const size_t need = (size_t)width * (size_t)height * 2u;
    if (len < need) {
        return;
    }
    uint8_t* copy = (uint8_t*)heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) {
        copy = (uint8_t*)heap_caps_malloc(need, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!copy) {
        return;
    }
    memcpy(copy, rgb565, need);

    FaceFrameJob job;
    job.w = width;
    job.h = height;
    job.data = copy;
    job.len = need;
    if (xQueueSend(s_queue, &job, 0) != pdTRUE) {
        heap_caps_free(copy);
    }
}

size_t DeepDogFaceAiFormatJson(char* buf, size_t buf_size) {
    if (!buf || buf_size < 64) {
        return 0;
    }
    DeepDogFaceSnapshot snap{};
    {
        std::lock_guard<std::mutex> lock(s_snap_mu);
        snap = s_snapshot;
    }
    const bool has = snap.count > 0;
    int n = snprintf(buf, buf_size,
                     "{\"enabled\":true,\"feature_on\":%s,\"w\":%u,\"h\":%u,\"has_face\":%s,\"n\":%d,\"ts\":%u,\"faces\":[",
                     snap.feature_enabled ? "true" : "false", (unsigned)snap.frame_w, (unsigned)snap.frame_h,
                     has ? "true" : "false", snap.count, (unsigned)snap.ts_ms);
    if (n < 0 || (size_t)n >= buf_size) {
        return 0;
    }
    size_t pos = (size_t)n;
    for (int i = 0; i < snap.count && i < 8; i++) {
        const DeepDogFaceBox& b = snap.faces[i];
        const float fw = snap.frame_w > 0 ? static_cast<float>(snap.frame_w) : 1.f;
        const float fh = snap.frame_h > 0 ? static_cast<float>(snap.frame_h) : 1.f;
        const float nx0 = b.x0 / fw;
        const float ny0 = b.y0 / fh;
        const float nx1 = b.x1 / fw;
        const float ny1 = b.y1 / fh;
        const float cx = (nx0 + nx1) * 0.5f;
        const float cy = (ny0 + ny1) * 0.5f;
        int w = snprintf(buf + pos, buf_size - pos,
                         "%s{\"x0\":%.4f,\"y0\":%.4f,\"x1\":%.4f,\"y1\":%.4f,\"cx\":%.4f,\"cy\":%.4f,\"score\":%.4f}",
                         i ? "," : "", nx0, ny0, nx1, ny1, cx, cy, b.score);
        if (w < 0 || (size_t)w >= buf_size - pos) {
            break;
        }
        pos += (size_t)w;
    }
    if (pos + 8 < buf_size) {
        memcpy(buf + pos, "]}", 3);
        pos += 2;
    }
    return pos;
}

#else  // stub

bool DeepDogFaceAiRuntimeStart() {
    return true;
}
void DeepDogFaceAiRuntimeStop() {}
void DeepDogFaceAiSetEnabled(bool) {}
bool DeepDogFaceAiIsEnabled() {
    return false;
}
void DeepDogFaceAiSubmitFrameIfDue(const uint8_t*, size_t, uint16_t, uint16_t) {}

size_t DeepDogFaceAiFormatJson(char* buf, size_t buf_size) {
    if (!buf || buf_size == 0) {
        return 0;
    }
    const char* s = "{\"enabled\":false,\"feature_on\":false,\"w\":0,\"h\":0,\"has_face\":false,\"n\":0,\"ts\":0,\"faces\":[]}";
    size_t i = 0;
    for (; s[i] && i + 1 < buf_size; i++) {
        buf[i] = s[i];
    }
    buf[i] = '\0';
    return i;
}

#endif
