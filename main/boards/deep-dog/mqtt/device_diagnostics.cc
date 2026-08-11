#include "mqtt/device_diagnostics.h"

#include <cJSON.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#if CONFIG_FREERTOS_USE_TRACE_FACILITY

namespace {

void AppendTasksArray(cJSON* root) {
    UBaseType_t count = uxTaskGetNumberOfTasks();
    if (count == 0) {
        return;
    }
    TaskStatus_t* status = static_cast<TaskStatus_t*>(malloc(sizeof(TaskStatus_t) * count));
    if (!status) {
        return;
    }
    const UBaseType_t got = uxTaskGetSystemState(status, count, nullptr);
    cJSON* arr = cJSON_CreateArray();
    if (!arr) {
        free(status);
        return;
    }
    for (UBaseType_t i = 0; i < got; ++i) {
        cJSON* o = cJSON_CreateObject();
        if (!o) {
            continue;
        }
        cJSON_AddStringToObject(o, "name", status[i].pcTaskName);
        cJSON_AddNumberToObject(o, "prio", static_cast<double>(status[i].uxCurrentPriority));
        cJSON_AddNumberToObject(o, "stack_hwm", static_cast<double>(status[i].usStackHighWaterMark));
        cJSON_AddNumberToObject(o, "state", static_cast<double>(status[i].eCurrentState));
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddItemToObject(root, "tasks", arr);
    cJSON_AddNumberToObject(root, "task_count", static_cast<double>(got));
    free(status);
}

}  // namespace

void DeepDogDeviceDiagnosticsAppendTasks(cJSON* root) {
    if (!root) {
        return;
    }
    AppendTasksArray(root);
}

#else

void DeepDogDeviceDiagnosticsAppendTasks(cJSON* root) {
    (void)root;
}

#endif
