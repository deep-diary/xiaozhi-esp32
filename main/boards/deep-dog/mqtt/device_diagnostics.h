#pragma once

struct cJSON;

/** 向 device/status JSON 追加 `tasks[]`（需 CONFIG_FREERTOS_USE_TRACE_FACILITY）。 */
void DeepDogDeviceDiagnosticsAppendTasks(cJSON* root);
