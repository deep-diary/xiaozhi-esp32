#include "sdkconfig.h"

#include "deep_dog_face_detect.h"
#include "face_ai_bridge.h"
#include "face_ai_config.h"
#include "face_ai_types.h"
#include "mqtt/memory_report.h"
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
#include <freertos/idf_additions.h>
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
static std::atomic<bool> s_user_enabled{DEEP_DOG_FACE_AI_DEFAULT_ENABLED != 0};
static std::atomic<bool> s_recognition_enabled{true};
static std::atomic<bool> s_runtime_started{false};
static std::atomic<bool> s_runtime_start_inflight{false};
static std::atomic<int> s_detect_interval_ms{DEEP_DOG_FACE_AI_MIN_INTERVAL_MS};
static std::atomic<uint8_t> s_pipeline{static_cast<uint8_t>(DeepDogFacePipeline::Live)};
static std::atomic<bool> s_vision_rtsp_active{false};
static int64_t s_last_submit_us = 0;
static int64_t s_last_recog_us = 0;
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
    if (side > (float)DEEP_DOG_FACE_IMMICH_MAX_CROP_PX) {
        side = (float)DEEP_DOG_FACE_IMMICH_MAX_CROP_PX;
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
    // 优先带 padding 的脸部裁剪（多人时避免整帧绑错人）
    // VGA 整帧 JPEG 易与 MJPEG 争抢 heap（image_to_jpeg alloc fail），仅小分辨率回退整帧
    if (!CropFaceToJpeg(rgb565, w, h, primary, &jpeg, &jpeg_len)) {
        const bool allow_full =
            ((uint32_t)w * (uint32_t)h) <= (uint32_t)DEEP_DOG_FACE_IMMICH_FULLFRAME_MAX_PX;
        if (allow_full) {
            ESP_LOGW(TAG, "immich crop failed, fallback full frame local_id=%d", primary.local_id);
            if (!FullFrameToJpeg(rgb565, w, h, &jpeg, &jpeg_len)) {
                ESP_LOGW(TAG, "immich jpeg failed local_id=%d", primary.local_id);
                return;
            }
        } else {
            ESP_LOGW(TAG, "immich crop failed local_id=%d (skip full-frame @%ux%u)", primary.local_id,
                     (unsigned)w, (unsigned)h);
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

/** 检测框 IoU（像素坐标），用于 live 跳过识别时沿用上次 ID/人名。 */
static float BoxIou(const DeepDogFaceBox& a, const DeepDogFaceBox& b) {
    const float ix0 = std::max(a.x0, b.x0);
    const float iy0 = std::max(a.y0, b.y0);
    const float ix1 = std::min(a.x1, b.x1);
    const float iy1 = std::min(a.y1, b.y1);
    const float iw = ix1 - ix0;
    const float ih = iy1 - iy0;
    if (iw <= 0.f || ih <= 0.f) {
        return 0.f;
    }
    const float inter = iw * ih;
    const float area_a = std::max(0.f, a.x1 - a.x0) * std::max(0.f, a.y1 - a.y0);
    const float area_b = std::max(0.f, b.x1 - b.x0) * std::max(0.f, b.y1 - b.y0);
    const float uni = area_a + area_b - inter;
    return uni > 0.f ? (inter / uni) : 0.f;
}

/**
 * live 模式下识别降频时：按 IoU 把上一帧已识别的 local_id/display_name 挂到本帧检测框，
 * 避免中间帧冲掉人名导致前端大多显示「未识别」。TTL 对齐 RECOG_SESSION_MS。
 */
static void StickyApplyPrevIds(std::vector<DeepDogFaceBox>* boxes, DeepDogFaceSnapshot* snap) {
    if (!boxes || boxes->empty() || !snap) {
        return;
    }
    DeepDogFaceSnapshot prev{};
    {
        std::lock_guard<std::mutex> lock(s_snap_mu);
        prev = s_snapshot;
    }
    if (prev.count <= 0) {
        return;
    }
    const int64_t age_ms = static_cast<int64_t>(snap->ts_ms) - static_cast<int64_t>(prev.ts_ms);
    if (age_ms < 0 || age_ms > static_cast<int64_t>(DEEP_DOG_FACE_RECOG_SESSION_MS)) {
        return;
    }

    constexpr float kMinIou = 0.3f;
    const int prev_n = prev.count > 8 ? 8 : prev.count;
    uint8_t used[8] = {};
    for (auto& box : *boxes) {
        int best = -1;
        float best_iou = kMinIou;
        for (int i = 0; i < prev_n; i++) {
            if (used[i] || prev.faces[i].local_id <= 0) {
                continue;
            }
            const float iou = BoxIou(box, prev.faces[i]);
            if (iou > best_iou) {
                best_iou = iou;
                best = i;
            }
        }
        if (best < 0) {
            continue;
        }
        used[best] = 1;
        const DeepDogFaceBox& p = prev.faces[best];
        box.local_id = p.local_id;
        box.recognize_source = p.recognize_source;
        strncpy(box.display_name, p.display_name, sizeof(box.display_name) - 1);
        box.display_name[sizeof(box.display_name) - 1] = '\0';
    }

    const DeepDogFaceBox* primary = nullptr;
    for (const auto& box : *boxes) {
        if (box.local_id <= 0) {
            continue;
        }
        if (!primary || box.score > primary->score) {
            primary = &box;
        }
    }
    if (primary) {
        snap->primary_local_id = primary->local_id;
        strncpy(snap->primary_display_name, primary->display_name, sizeof(snap->primary_display_name) - 1);
        snap->primary_display_name[sizeof(snap->primary_display_name) - 1] = '\0';
        snap->primary_source = primary->recognize_source;
    }
}

static void FaceAiTask(void* /*arg*/) {
    FaceFrameJob job{};
#if DEEP_DOG_FACE_IMMICH_ENABLE
    int immich_retry_ticks = 0;
#endif
    for (;;) {
#if DEEP_DOG_FACE_IMMICH_ENABLE
        if (!DeepDogImmichIsWorkerReady() && ++immich_retry_ticks >= 120) {
            immich_retry_ticks = 0;
            (void)DeepDogImmichInit();
        }
#endif
        if (xQueueReceive(s_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (job.data == nullptr || job.len == 0) {
            continue;
        }
        // 给 IDLE 喘息：识别卷积会长时间占满 CPU0，曾导致 TWDT → InstrFetchProhibited → 软重启后摄像头挂死
        vTaskDelay(pdMS_TO_TICKS(5));

        std::vector<DeepDogFaceBox> boxes;
        DeepDogFaceSnapshot snap{};
        snap.frame_w = job.w;
        snap.frame_h = job.h;
        snap.ts_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        snap.feature_enabled = s_user_enabled.load(std::memory_order_relaxed);

        if (s_user_enabled.load(std::memory_order_relaxed)) {
#if DEEP_DOG_FACE_RECOG_ENABLE
            std::list<dl::detect::result_t> raw;
            const int64_t t_detect0 = esp_timer_get_time();
            const bool detected = DeepDogFaceDetectRun(job.data, job.len, job.w, job.h, &boxes, &raw);
            const int detect_ms = static_cast<int>((esp_timer_get_time() - t_detect0) / 1000);
            if (detected) {
                vTaskDelay(pdMS_TO_TICKS(5));
                const DeepDogFacePipeline pipe =
                    static_cast<DeepDogFacePipeline>(s_pipeline.load(std::memory_order_relaxed));
                const int64_t now_us = esp_timer_get_time();
                const bool recog_on = s_recognition_enabled.load(std::memory_order_relaxed);
                int recog_min_ms = DEEP_DOG_FACE_RECOG_MIN_INTERVAL_MS;
#if DEEP_DOG_FACE_AI_DURING_RTSP
                if (s_vision_rtsp_active.load(std::memory_order_relaxed) && recog_on &&
                    recog_min_ms < DEEP_DOG_FACE_AI_RTSP_RECOG_MIN_INTERVAL_MS) {
                    recog_min_ms = DEEP_DOG_FACE_AI_RTSP_RECOG_MIN_INTERVAL_MS;
                }
#endif
                const bool run_recog =
                    recog_on &&
                    ((pipe == DeepDogFacePipeline::Identity) ||
                     ((now_us - s_last_recog_us) >= (int64_t)recog_min_ms * 1000));
                int recog_ms = -1;
                if (run_recog && !boxes.empty()) {
                    dl::image::img_t img{};
                    uint8_t* owned = nullptr;
                    if (DeepDogFaceDetectMakeImg(job.data, job.len, job.w, job.h, &img, &owned)) {
                        taskYIELD();
                        const int64_t t_recog0 = esp_timer_get_time();
                        DeepDogFaceRecognizeProcess(img, raw, &boxes, &snap);
                        taskYIELD();
                        recog_ms = static_cast<int>((esp_timer_get_time() - t_recog0) / 1000);
                        s_last_recog_us = now_us;
                        if (owned) {
                            heap_caps_free(owned);
                        }
                        vTaskDelay(pdMS_TO_TICKS(5));
                        for (const auto& b : boxes) {
                            if (b.local_id > 0 && recog_on) {
                                MaybeRequestImmichName(job.data, job.w, job.h, b);
                            }
                        }
                    }
                } else if (!boxes.empty()) {
                    StickyApplyPrevIds(&boxes, &snap);
                }
                static int s_timing_logs = 0;
                if (s_timing_logs < 16) {
                    ESP_LOGI(TAG, "timing detect_ms=%d recog_ms=%d boxes=%u sticky=%d", detect_ms, recog_ms,
                             static_cast<unsigned>(boxes.size()),
                             (run_recog || boxes.empty()) ? 0 : 1);
                    ++s_timing_logs;
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
        // 帧间让出 CPU，避免连续推理饿死 IDLE0
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void TeardownRuntimeTask() {
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
}

static void TeardownRuntimeAll() {
    TeardownRuntimeTask();
#if DEEP_DOG_FACE_IMMICH_ENABLE
    DeepDogImmichDeinit();
#endif
#if DEEP_DOG_FACE_RECOG_ENABLE
    DeepDogFaceRecognizeDeinit();
#endif
    DeepDogFaceDetectDeinit();
}

static void LogRuntimeStartFail(const char* step) {
    ESP_LOGW(TAG, "runtime start failed at %s (largest_int=%u free_int=%u)", step,
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

bool DeepDogFaceAiRuntimeStart() {
    if (s_runtime_started.load(std::memory_order_acquire)) {
        return true;
    }
    // 先占 task 栈再加载模型，避免 RTSP 等场景下 internal 碎片化导致 xTaskCreate 失败
    s_queue = xQueueCreate(1, sizeof(FaceFrameJob));
    if (!s_queue) {
        LogRuntimeStartFail("queue");
        return false;
    }
    if (xTaskCreate(FaceAiTask, "dog_face_ai", DEEP_DOG_FACE_AI_TASK_STACK, nullptr, 2, &s_task) != pdPASS) {
        LogRuntimeStartFail("dog_face_ai task");
        vQueueDelete(s_queue);
        s_queue = nullptr;
        return false;
    }
    if (!DeepDogFaceDetectInit()) {
        LogRuntimeStartFail("detect init");
        TeardownRuntimeAll();
        return false;
    }
#if DEEP_DOG_FACE_RECOG_ENABLE
    if (!DeepDogFaceRecognizeInit()) {
        ESP_LOGW(TAG, "face recognize init failed (detect still available)");
    }
#endif
    s_runtime_started.store(true, std::memory_order_release);
    ESP_LOGI(TAG, "runtime started (queue=1, interval>=%d ms, recog_min=%d ms, during_rtsp=%d, recog=%d immich=%d)",
             DEEP_DOG_FACE_AI_MIN_INTERVAL_MS, DEEP_DOG_FACE_RECOG_MIN_INTERVAL_MS, DEEP_DOG_FACE_AI_DURING_RTSP,
             DEEP_DOG_FACE_RECOG_ENABLE, DEEP_DOG_FACE_IMMICH_ENABLE);
    DeepDogMemoryReportLog("face_ready");
    return true;
}

void DeepDogFaceAiRuntimeStop() {
    if (!s_runtime_started.load()) {
        return;
    }
    TeardownRuntimeAll();
    s_runtime_started.store(false, std::memory_order_release);
}

#if DEEP_DOG_FACE_IMMICH_ENABLE
static void ScheduleImmichWorkerAfterBoot();
#endif

static void FaceRuntimeStartWorker(void*) {
    if (!s_user_enabled.load(std::memory_order_acquire)) {
        s_runtime_start_inflight.store(false, std::memory_order_release);
        vTaskDelete(nullptr);
        return;
    }
    if (s_runtime_started.load(std::memory_order_acquire)) {
        s_runtime_start_inflight.store(false, std::memory_order_release);
        vTaskDelete(nullptr);
        return;
    }
    const bool started = DeepDogFaceAiRuntimeStart();
    if (!started) {
        ESP_LOGW(TAG, "deferred runtime start failed");
    } else if (s_user_enabled.load(std::memory_order_acquire)) {
#if DEEP_DOG_FACE_IMMICH_ENABLE
        ScheduleImmichWorkerAfterBoot();
#endif
    }
    s_runtime_start_inflight.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
}

#if DEEP_DOG_FACE_IMMICH_ENABLE
/** face_boot 退出释放 16KB internal 后再建 dog_immich（避免模型加载期 largest_int<8K 误报失败）。 */
static void ImmichLateStartTask(void* /*arg*/) {
    vTaskDelay(pdMS_TO_TICKS(150));
    if (DeepDogImmichIsWorkerReady()) {
        vTaskDelete(nullptr);
        return;
    }
    if (DeepDogImmichInit()) {
        ESP_LOGI(TAG, "immich worker started (post-boot)");
    } else {
        ESP_LOGW(TAG, "immich post-boot init failed (free_int=%u largest_int=%u)",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    }
    vTaskDelete(nullptr);
}

static void ScheduleImmichWorkerAfterBoot() {
    if (xTaskCreate(ImmichLateStartTask, "immich_late", 4096, nullptr, 2, nullptr) != pdPASS) {
        ESP_LOGW(TAG, "immich_late task create failed");
    }
}
#endif

void DeepDogFaceAiSetEnabled(bool on) {
    if (on) {
        if (!s_runtime_started.load(std::memory_order_acquire)) {
            if (!s_runtime_start_inflight.exchange(true, std::memory_order_acq_rel)) {
                // 模型加载栈深；禁止在 mqtt_task（~6KB）上同步 RuntimeStart
                if (xTaskCreate(FaceRuntimeStartWorker, "face_boot", DEEP_DOG_FACE_BOOT_TASK_STACK, nullptr, 2,
                                nullptr) != pdPASS) {
                    s_runtime_start_inflight.store(false, std::memory_order_release);
                    ESP_LOGW(TAG, "SetEnabled(true) failed: face_boot task");
                    return;
                }
            }
        }
    } else {
        s_runtime_start_inflight.store(false, std::memory_order_release);
    }
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

void DeepDogFaceAiSetRecognitionEnabled(bool on) {
    s_recognition_enabled.store(on, std::memory_order_relaxed);
    ESP_LOGI(TAG, "recognition_enabled=%d", on ? 1 : 0);
}

bool DeepDogFaceAiIsRecognitionEnabled() {
    return s_recognition_enabled.load(std::memory_order_relaxed);
}

void DeepDogFaceAiSetPipeline(DeepDogFacePipeline pipeline) {
    s_pipeline.store(static_cast<uint8_t>(pipeline), std::memory_order_relaxed);
    ESP_LOGI(TAG, "pipeline=%s", DeepDogFacePipelineStr(pipeline));
}

DeepDogFacePipeline DeepDogFaceAiGetPipeline() {
    return static_cast<DeepDogFacePipeline>(s_pipeline.load(std::memory_order_relaxed));
}

void DeepDogFaceAiSetDetectIntervalMs(int ms) {
    if (ms < DEEP_DOG_FACE_AI_INTERVAL_MIN_MS) {
        ms = DEEP_DOG_FACE_AI_INTERVAL_MIN_MS;
    }
    if (ms > DEEP_DOG_FACE_AI_INTERVAL_MAX_MS) {
        ms = DEEP_DOG_FACE_AI_INTERVAL_MAX_MS;
    }
    s_detect_interval_ms.store(ms, std::memory_order_relaxed);
    ESP_LOGI(TAG, "detect_interval_ms=%d", ms);
}

int DeepDogFaceAiGetDetectIntervalMs() {
    return s_detect_interval_ms.load(std::memory_order_relaxed);
}

void DeepDogFaceAiSetVisionRtspActive(bool active) {
    s_vision_rtsp_active.store(active, std::memory_order_relaxed);
}

bool DeepDogFaceAiIsVisionRtspActive() {
    return s_vision_rtsp_active.load(std::memory_order_relaxed);
}

bool DeepDogFaceAiClearDb() {
#if DEEP_DOG_FACE_RECOG_ENABLE
    const bool ok = DeepDogFaceRecognizeClearAll();
#else
    const bool ok = true;
#endif
    {
        std::lock_guard<std::mutex> lock(s_snap_mu);
        s_snapshot.count = 0;
        s_snapshot.primary_local_id = 0;
        s_snapshot.primary_display_name[0] = '\0';
        s_snapshot.primary_source = DeepDogFaceRecognizeSource::None;
        for (auto& f : s_snapshot.faces) {
            f = DeepDogFaceBox{};
        }
    }
    s_last_recog_us = 0;
    return ok;
}

void DeepDogFaceAiSubmitFrameIfDue(const uint8_t* rgb565, size_t len, uint16_t width, uint16_t height) {
    if (!s_runtime_started.load(std::memory_order_acquire) || !s_queue || !rgb565 || width == 0 || height == 0) {
        return;
    }
    if (!s_user_enabled.load(std::memory_order_relaxed)) {
        return;
    }
    const int64_t now = esp_timer_get_time();
    int interval_ms = s_detect_interval_ms.load(std::memory_order_relaxed);
#if DEEP_DOG_FACE_AI_DURING_RTSP
    if (s_vision_rtsp_active.load(std::memory_order_relaxed) &&
        interval_ms < DEEP_DOG_FACE_AI_RTSP_MIN_INTERVAL_MS) {
        interval_ms = DEEP_DOG_FACE_AI_RTSP_MIN_INTERVAL_MS;
    }
#endif
    if ((now - s_last_submit_us) < (int64_t)interval_ms * 1000) {
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

void DeepDogFaceAiCopySnapshot(DeepDogFaceSnapshot* out) {
    if (!out) {
        return;
    }
    std::lock_guard<std::mutex> lock(s_snap_mu);
    *out = s_snapshot;
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
void DeepDogFaceAiSetRecognitionEnabled(bool) {}
bool DeepDogFaceAiIsRecognitionEnabled() {
    return false;
}
void DeepDogFaceAiSetPipeline(DeepDogFacePipeline) {}
DeepDogFacePipeline DeepDogFaceAiGetPipeline() {
    return DeepDogFacePipeline::Live;
}
void DeepDogFaceAiSetDetectIntervalMs(int) {}
int DeepDogFaceAiGetDetectIntervalMs() {
    return DEEP_DOG_FACE_AI_MIN_INTERVAL_MS;
}
void DeepDogFaceAiSetVisionRtspActive(bool) {}
bool DeepDogFaceAiIsVisionRtspActive() {
    return false;
}
bool DeepDogFaceAiClearDb() {
    return true;
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

void DeepDogFaceAiCopySnapshot(DeepDogFaceSnapshot* out) {
    if (out) {
        *out = DeepDogFaceSnapshot{};
    }
}

#endif
