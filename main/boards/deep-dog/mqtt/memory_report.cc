#include "mqtt/memory_report.h"

#include "face_ai_config.h"
#include "vision/vision_config.h"

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstring>
#include <vector>

#define TAG "dog_mem_rpt"

namespace {

constexpr size_t kLowInternalHeapBytes = 32 * 1024;
constexpr int kLogTopTasks = 8;

struct TaskRow {
    char name[configMAX_TASK_NAME_LEN];
    UBaseType_t prio;
    uint16_t stack_hwm;
    eTaskState state;
};

struct StackHint {
    const char* name;
    uint32_t alloc_bytes;
    const char* domain;
};

uint32_t VisionHubStackBytes() {
#if DEEP_DOG_VISION_CODEC_H264
    return 49152;
#else
    return 12288;
#endif
}

const StackHint* LookupStackHint(const char* name) {
    static const StackHint hints[] = {
        {"dog_face_ai", DEEP_DOG_FACE_AI_TASK_STACK, "psram"},
        {"dog_immich", DEEP_DOG_FACE_IMMICH_TASK_STACK, "internal"},
        {"face_persist", DEEP_DOG_FACE_PERSIST_TASK_STACK, "internal"},
        {"face_facedb", DEEP_DOG_FACE_FACEDB_TASK_STACK, "internal"},
        {"face_boot", DEEP_DOG_FACE_BOOT_TASK_STACK, "internal"},
        {"immich_late", 4096, "internal"},
        {"face_ai_defer", 3072, "internal"},
        {"opus_codec", 2048 * 12, "internal"},
        {"vision_hub", VisionHubStackBytes(), "psram"},
        {"audio_input", 4096 * 2, "internal"},
        {"mqtt_task", 6144, "internal"},
        {"touch_btn_task", 4096, "internal"},
        {"esp_timer", 4096, "internal"},
    };
    for (const auto& h : hints) {
        if (strcmp(h.name, name) == 0) {
            return &h;
        }
    }
    return nullptr;
}

void AppendMemBucket(cJSON* parent, const char* key, uint32_t caps) {
    const size_t free_b = heap_caps_get_free_size(caps);
    const size_t min_b = heap_caps_get_minimum_free_size(caps);
    const size_t total_b = heap_caps_get_total_size(caps);
    cJSON* obj = cJSON_AddObjectToObject(parent, key);
    cJSON_AddNumberToObject(obj, "free", static_cast<double>(free_b));
    cJSON_AddNumberToObject(obj, "min", static_cast<double>(min_b));
    cJSON_AddNumberToObject(obj, "total", static_cast<double>(total_b));
    cJSON_AddNumberToObject(obj, "largest_free",
                            static_cast<double>(heap_caps_get_largest_free_block(caps)));
    if (total_b >= free_b) {
        cJSON_AddNumberToObject(obj, "used", static_cast<double>(total_b - free_b));
    }
}

#if CONFIG_FREERTOS_USE_TRACE_FACILITY

bool TaskRowLess(const TaskRow& a, const TaskRow& b) {
    return a.stack_hwm < b.stack_hwm;
}

void AppendTaskObject(cJSON* arr, const TaskRow& row) {
    cJSON* o = cJSON_CreateObject();
    if (!o) {
        return;
    }
    cJSON_AddStringToObject(o, "name", row.name);
    cJSON_AddNumberToObject(o, "prio", static_cast<double>(row.prio));
    cJSON_AddNumberToObject(o, "stack_hwm", static_cast<double>(row.stack_hwm));
    cJSON_AddNumberToObject(o, "state", static_cast<double>(row.state));
    const StackHint* hint = LookupStackHint(row.name);
    if (hint) {
        cJSON_AddStringToObject(o, "stack_domain", hint->domain);
        if (hint->alloc_bytes > row.stack_hwm) {
            cJSON_AddNumberToObject(o, "stack_used_est",
                                    static_cast<double>(hint->alloc_bytes - row.stack_hwm));
        }
    } else {
        cJSON_AddStringToObject(o, "stack_domain", "unknown");
    }
    cJSON_AddItemToArray(arr, o);
}

void CollectSortedTasks(std::vector<TaskRow>* out) {
    if (!out) {
        return;
    }
    out->clear();
    const UBaseType_t count = uxTaskGetNumberOfTasks();
    if (count == 0) {
        return;
    }
    TaskStatus_t* status = static_cast<TaskStatus_t*>(malloc(sizeof(TaskStatus_t) * count));
    if (!status) {
        return;
    }
    const UBaseType_t got = uxTaskGetSystemState(status, count, nullptr);
    out->reserve(got);
    for (UBaseType_t i = 0; i < got; ++i) {
        TaskRow row{};
        strncpy(row.name, status[i].pcTaskName, sizeof(row.name) - 1);
        row.prio = status[i].uxCurrentPriority;
        row.stack_hwm = status[i].usStackHighWaterMark;
        row.state = status[i].eCurrentState;
        out->push_back(row);
    }
    free(status);
    std::sort(out->begin(), out->end(), TaskRowLess);
}

void AppendTasksArray(cJSON* root) {
    std::vector<TaskRow> rows;
    CollectSortedTasks(&rows);
    if (rows.empty()) {
        return;
    }
    cJSON* arr = cJSON_CreateArray();
    if (!arr) {
        return;
    }
    for (const auto& row : rows) {
        AppendTaskObject(arr, row);
    }
    cJSON_AddItemToObject(root, "tasks", arr);
    cJSON_AddNumberToObject(root, "task_count", static_cast<double>(rows.size()));
}

void LogTopTasks() {
    std::vector<TaskRow> rows;
    CollectSortedTasks(&rows);
    const int n = static_cast<int>(rows.size() < static_cast<size_t>(kLogTopTasks) ? rows.size()
                                                                                   : kLogTopTasks);
    for (int i = 0; i < n; ++i) {
        const TaskRow& row = rows[static_cast<size_t>(i)];
        const StackHint* hint = LookupStackHint(row.name);
        if (hint) {
            ESP_LOGI(TAG, "  task %-16s hwm=%u domain=%s alloc=%u", row.name, row.stack_hwm, hint->domain,
                     hint->alloc_bytes);
        } else {
            ESP_LOGI(TAG, "  task %-16s hwm=%u domain=unknown", row.name, row.stack_hwm);
        }
    }
}

#endif  // CONFIG_FREERTOS_USE_TRACE_FACILITY

void LogMemBucket(const char* label, uint32_t caps) {
    const size_t free_b = heap_caps_get_free_size(caps);
    const size_t min_b = heap_caps_get_minimum_free_size(caps);
    const size_t total_b = heap_caps_get_total_size(caps);
    const size_t largest = heap_caps_get_largest_free_block(caps);
    const float pct = total_b > 0 ? (100.0f * static_cast<float>(free_b) / static_cast<float>(total_b)) : 0.0f;
    ESP_LOGI(TAG, "  %s: free=%u min=%u total=%u largest=%u (%.1f%% free)", label, (unsigned)free_b,
             (unsigned)min_b, (unsigned)total_b, (unsigned)largest, static_cast<double>(pct));
}

}  // namespace

void DeepDogMemoryReportAppend(cJSON* root) {
    if (!root) {
        return;
    }
    cJSON* mem = cJSON_AddObjectToObject(root, "mem");
    AppendMemBucket(mem, "internal", MALLOC_CAP_INTERNAL);
    AppendMemBucket(mem, "psram", MALLOC_CAP_SPIRAM);
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
    AppendTasksArray(root);
#endif
}

void DeepDogMemoryReportAppendTasks(cJSON* root) {
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
    AppendTasksArray(root);
#else
    (void)root;
#endif
}

void DeepDogMemoryReportLog(const char* phase) {
    ESP_LOGI(TAG, "memory report [%s]", phase ? phase : "?");
    LogMemBucket("internal", MALLOC_CAP_INTERNAL);
    LogMemBucket("psram", MALLOC_CAP_SPIRAM);
    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (internal_free < kLowInternalHeapBytes) {
        ESP_LOGW(TAG, "  health: low_internal_heap (free=%u < %u)", (unsigned)internal_free,
                 (unsigned)kLowInternalHeapBytes);
    }
#if CONFIG_FREERTOS_USE_TRACE_FACILITY
    LogTopTasks();
#else
    ESP_LOGW(TAG, "  tasks: unavailable (CONFIG_FREERTOS_USE_TRACE_FACILITY=n)");
#endif
}
