#include "sdkconfig.h"

#include "deep_dog_face_detect.h"
#include "face_ai_bridge.h"
#include "face_ai_config.h"
#include "face_ai_types.h"
#include "face_recognize.h"
#include "immich_client.h"
#include "image_to_jpeg.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <list>
#include <mutex>
#include <vector>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#if DEEP_DOG_FACE_AI_ENABLE && defined(CONFIG_IDF_TARGET_ESP32S3)

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

/** 从 RGB565 紧密帧裁剪人脸并 JPEG；成功则 *out_jpeg 由调用方/Immich 释放。 */
static bool CropFaceToJpeg(const uint8_t* rgb565, uint16_t fw, uint16_t fh, const DeepDogFaceBox& box,
                           uint8_t** out_jpeg, size_t* out_len) {
    if (!rgb565 || !out_jpeg || !out_len || fw == 0 || fh == 0) {
        return false;
    }
    *out_jpeg = nullptr;
    *out_len = 0;

    float x0 = box.x0;
    float y0 = box.y0;
    float x1 = box.x1;
    float y1 = box.y1;
    if (x1 <= x0 || y1 <= y0) {
        return false;
    }
    float bw = x1 - x0;
    float bh = y1 - y0;
    // Immich 对过小裁剪识别差：扩大 padding，并保证最短边 >= MIN_CROP
    float pad = 0.35f;
    float side = std::max(bw, bh) * (1.f + 2.f * pad);
    if (side < (float)DEEP_DOG_FACE_IMMICH_MIN_CROP_PX) {
        side = (float)DEEP_DOG_FACE_IMMICH_MIN_CROP_PX;
    }
    const float cx = (x0 + x1) * 0.5f;
    const float cy = (y0 + y1) * 0.5f;
    x0 = cx - side * 0.5f;
    y0 = cy - side * 0.5f;
    x1 = cx + side * 0.5f;
    y1 = cy + side * 0.5f;
    if (x0 < 0) {
        x1 -= x0;
        x0 = 0;
    }
    if (y0 < 0) {
        y1 -= y0;
        y0 = 0;
    }
    if (x1 > (float)fw) {
        x0 -= (x1 - (float)fw);
        x1 = (float)fw;
    }
    if (y1 > (float)fh) {
        y0 -= (y1 - (float)fh);
        y1 = (float)fh;
    }
    x0 = std::max(0.f, x0);
    y0 = std::max(0.f, y0);
    x1 = std::min((float)fw, x1);
    y1 = std::min((float)fh, y1);

    int ix0 = (int)x0;
    int iy0 = (int)y0;
    int ix1 = (int)x1;
    int iy1 = (int)y1;
    // JPEG 编码器对奇数宽高更敏感，对齐到偶数
    if ((ix1 - ix0) & 1) {
        if (ix1 < (int)fw) {
            ix1++;
        } else if (ix0 > 0) {
            ix0--;
        }
    }
    if ((iy1 - iy0) & 1) {
        if (iy1 < (int)fh) {
            iy1++;
        } else if (iy0 > 0) {
            iy0--;
        }
    }
    if (ix1 <= ix0 + 4 || iy1 <= iy0 + 4) {
        return false;
    }
    const int cw = ix1 - ix0;
    const int ch = iy1 - iy0;
    const size_t crop_bytes = (size_t)cw * (size_t)ch * 2u;
    uint8_t* crop = (uint8_t*)heap_caps_malloc(crop_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!crop) {
        crop = (uint8_t*)heap_caps_malloc(crop_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!crop) {
        ESP_LOGW(TAG, "immich crop alloc fail %dx%d", cw, ch);
        return false;
    }
    for (int y = 0; y < ch; y++) {
        const uint8_t* src = rgb565 + ((size_t)(iy0 + y) * (size_t)fw + (size_t)ix0) * 2u;
        memcpy(crop + (size_t)y * (size_t)cw * 2u, src, (size_t)cw * 2u);
    }

    uint8_t* jpeg = nullptr;
    size_t jpeg_len = 0;
    const bool ok = image_to_jpeg(crop, crop_bytes, (uint16_t)cw, (uint16_t)ch, V4L2_PIX_FMT_RGB565,
                                  (uint8_t)DEEP_DOG_FACE_IMMICH_JPEG_QUALITY, &jpeg, &jpeg_len);
    heap_caps_free(crop);
    if (!ok || !jpeg || jpeg_len == 0) {
        ESP_LOGW(TAG, "immich jpeg encode fail crop=%dx%d ok=%d", cw, ch, (int)ok);
        if (jpeg) {
            free(jpeg);
        }
        return false;
    }
    // image_to_jpeg 用 malloc；Immich 队列统一 heap_caps_free
    uint8_t* owned = (uint8_t*)heap_caps_malloc(jpeg_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!owned) {
        owned = (uint8_t*)heap_caps_malloc(jpeg_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!owned) {
        free(jpeg);
        return false;
    }
    memcpy(owned, jpeg, jpeg_len);
    free(jpeg);
    *out_jpeg = owned;
    *out_len = jpeg_len;
    ESP_LOGI(TAG, "immich jpeg ready crop=%dx%d bytes=%u", cw, ch, (unsigned)jpeg_len);
    return true;
}

/** 整帧 JPEG（裁剪失败时回退，便于 Immich 识别） */
static bool FullFrameToJpeg(const uint8_t* rgb565, uint16_t fw, uint16_t fh, uint8_t** out_jpeg, size_t* out_len) {
    if (!rgb565 || !out_jpeg || !out_len || fw == 0 || fh == 0) {
        return false;
    }
    *out_jpeg = nullptr;
    *out_len = 0;
    const size_t src_len = (size_t)fw * (size_t)fh * 2u;
    uint8_t* jpeg = nullptr;
    size_t jpeg_len = 0;
    if (!image_to_jpeg((uint8_t*)rgb565, src_len, fw, fh, V4L2_PIX_FMT_RGB565,
                       (uint8_t)DEEP_DOG_FACE_IMMICH_JPEG_QUALITY, &jpeg, &jpeg_len) ||
        !jpeg || jpeg_len == 0) {
        if (jpeg) {
            free(jpeg);
        }
        return false;
    }
    uint8_t* owned = (uint8_t*)heap_caps_malloc(jpeg_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!owned) {
        owned = (uint8_t*)heap_caps_malloc(jpeg_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!owned) {
        free(jpeg);
        return false;
    }
    memcpy(owned, jpeg, jpeg_len);
    free(jpeg);
    *out_jpeg = owned;
    *out_len = jpeg_len;
    return true;
}

static void MaybeRequestImmichName(const uint8_t* rgb565, uint16_t w, uint16_t h, const DeepDogFaceBox& primary) {
#if DEEP_DOG_FACE_IMMICH_ENABLE && DEEP_DOG_FACE_RECOG_ENABLE
    if (primary.local_id <= 0 || !DeepDogImmichIsConfigured()) {
        return;
    }
    const bool force = DeepDogImmichConsumeForceRefresh(primary.local_id);
    if (!force && !DeepDogFaceRecognizeNeedsImmichName(primary.local_id)) {
        return;
    }
    uint8_t* jpeg = nullptr;
    size_t jpeg_len = 0;
    // 优先带 padding 的脸部裁剪（多人时避免整帧绑错人）；失败再整帧
    if (!CropFaceToJpeg(rgb565, w, h, primary, &jpeg, &jpeg_len)) {
        ESP_LOGW(TAG, "immich crop failed, fallback full frame local_id=%d", primary.local_id);
        if (!FullFrameToJpeg(rgb565, w, h, &jpeg, &jpeg_len)) {
            ESP_LOGW(TAG, "immich jpeg failed local_id=%d", primary.local_id);
            return;
        }
    }
    if (!DeepDogImmichRequestName(primary.local_id, jpeg, jpeg_len, force)) {
        // RequestName 已释放 jpeg
    }
#else
    (void)rgb565;
    (void)w;
    (void)h;
    (void)primary;
#endif
}

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
        DeepDogFaceSnapshot snap{};
        snap.frame_w = job.w;
        snap.frame_h = job.h;
        snap.ts_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        snap.feature_enabled = s_user_enabled.load(std::memory_order_relaxed);

        if (s_user_enabled.load(std::memory_order_relaxed)) {
#if DEEP_DOG_FACE_RECOG_ENABLE
            std::list<dl::detect::result_t> raw;
            if (DeepDogFaceDetectRun(job.data, job.len, job.w, job.h, &boxes, &raw)) {
                dl::image::img_t img{};
                uint8_t* owned = nullptr;
                if (!boxes.empty() && DeepDogFaceDetectMakeImg(job.data, job.len, job.w, job.h, &img, &owned)) {
                    DeepDogFaceRecognizeProcess(img, raw, &boxes, &snap);
                    if (owned) {
                        heap_caps_free(owned);
                    }
                    // Immich 队列深度 1：每帧为尚无真名的脸各尝试投递一次（满则丢弃）
                    for (const auto& b : boxes) {
                        if (b.local_id > 0) {
                            MaybeRequestImmichName(job.data, job.w, job.h, b);
                        }
                    }
                }
            }
#else
            (void)DeepDogFaceDetectRun(job.data, job.len, job.w, job.h, &boxes, nullptr);
#endif
        }
        heap_caps_free(job.data);

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
#if DEEP_DOG_FACE_RECOG_ENABLE
    if (!DeepDogFaceRecognizeInit()) {
        ESP_LOGW(TAG, "face recognize init failed (detect still available)");
    }
#endif
#if DEEP_DOG_FACE_IMMICH_ENABLE
    if (!DeepDogImmichInit()) {
        ESP_LOGW(TAG, "immich worker init failed (local id still available)");
    }
#endif
    s_queue = xQueueCreate(1, sizeof(FaceFrameJob));
    if (!s_queue) {
#if DEEP_DOG_FACE_IMMICH_ENABLE
        DeepDogImmichDeinit();
#endif
#if DEEP_DOG_FACE_RECOG_ENABLE
        DeepDogFaceRecognizeDeinit();
#endif
        DeepDogFaceDetectDeinit();
        s_runtime_started = false;
        return false;
    }
    // 识别 MFN ~250ms，加大栈
    if (xTaskCreate(FaceAiTask, "dog_face_ai", 12288, nullptr, 2, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = nullptr;
#if DEEP_DOG_FACE_IMMICH_ENABLE
        DeepDogImmichDeinit();
#endif
#if DEEP_DOG_FACE_RECOG_ENABLE
        DeepDogFaceRecognizeDeinit();
#endif
        DeepDogFaceDetectDeinit();
        s_runtime_started = false;
        return false;
    }
    ESP_LOGI(TAG, "runtime started (queue=1, interval>=%d ms, recog=%d immich=%d)", DEEP_DOG_FACE_AI_MIN_INTERVAL_MS,
             DEEP_DOG_FACE_RECOG_ENABLE, DEEP_DOG_FACE_IMMICH_ENABLE);
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
#if DEEP_DOG_FACE_IMMICH_ENABLE
    DeepDogImmichDeinit();
#endif
#if DEEP_DOG_FACE_RECOG_ENABLE
    DeepDogFaceRecognizeDeinit();
#endif
    DeepDogFaceDetectDeinit();
    s_runtime_started = false;
}

void DeepDogFaceAiSetEnabled(bool on) {
    s_user_enabled.store(on, std::memory_order_relaxed);
    if (!on) {
        std::lock_guard<std::mutex> lock(s_snap_mu);
        s_snapshot.count = 0;
        s_snapshot.feature_enabled = false;
        s_snapshot.primary_local_id = 0;
        s_snapshot.primary_display_name[0] = '\0';
        s_snapshot.primary_source = DeepDogFaceRecognizeSource::None;
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
    int n = snprintf(
        buf, buf_size,
        "{\"enabled\":true,\"feature_on\":%s,\"w\":%u,\"h\":%u,\"has_face\":%s,\"n\":%d,\"ts\":%u,"
        "\"local_id\":%d,\"display_name\":\"%s\",\"recognize_source\":\"%s\",\"faces\":[",
        snap.feature_enabled ? "true" : "false", (unsigned)snap.frame_w, (unsigned)snap.frame_h,
        has ? "true" : "false", snap.count, (unsigned)snap.ts_ms, snap.primary_local_id,
        snap.primary_display_name, DeepDogFaceRecognizeSourceStr(snap.primary_source));
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
                         "%s{\"x0\":%.4f,\"y0\":%.4f,\"x1\":%.4f,\"y1\":%.4f,\"cx\":%.4f,\"cy\":%.4f,\"score\":%.4f,"
                         "\"local_id\":%d,\"display_name\":\"%s\",\"recognize_source\":\"%s\"}",
                         i ? "," : "", nx0, ny0, nx1, ny1, cx, cy, b.score, b.local_id, b.display_name,
                         DeepDogFaceRecognizeSourceStr(b.recognize_source));
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

void DeepDogFaceAiOnImmichName(int local_id, const char* display_name) {
    if (local_id <= 0 || !display_name || !display_name[0]) {
        return;
    }
    std::lock_guard<std::mutex> lock(s_snap_mu);
    if (s_snapshot.primary_local_id == local_id) {
        strncpy(s_snapshot.primary_display_name, display_name, sizeof(s_snapshot.primary_display_name) - 1);
        s_snapshot.primary_display_name[sizeof(s_snapshot.primary_display_name) - 1] = '\0';
    }
    for (int i = 0; i < s_snapshot.count && i < 8; i++) {
        if (s_snapshot.faces[i].local_id == local_id) {
            strncpy(s_snapshot.faces[i].display_name, display_name, sizeof(s_snapshot.faces[i].display_name) - 1);
            s_snapshot.faces[i].display_name[sizeof(s_snapshot.faces[i].display_name) - 1] = '\0';
        }
    }
}

int DeepDogFaceAiPrimaryLocalId() {
    std::lock_guard<std::mutex> lock(s_snap_mu);
    return s_snapshot.primary_local_id;
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
    const char* s =
        "{\"enabled\":false,\"feature_on\":false,\"w\":0,\"h\":0,\"has_face\":false,\"n\":0,\"ts\":0,"
        "\"local_id\":0,\"display_name\":\"\",\"recognize_source\":\"none\",\"faces\":[]}";
    size_t i = 0;
    for (; s[i] && i + 1 < buf_size; i++) {
        buf[i] = s[i];
    }
    buf[i] = '\0';
    return i;
}

void DeepDogFaceAiOnImmichName(int, const char*) {}
int DeepDogFaceAiPrimaryLocalId() {
    return 0;
}

#endif
