#pragma once

#include <cstddef>

struct cJSON;

/** 向 JSON 追加 `mem`（含 largest_free/used）与按 stack_hwm 升序的 `tasks[]`。 */
void DeepDogMemoryReportAppend(cJSON* root);

/** 仅追加排序后的 `tasks[]` / `task_count`（供旧 diagnostics 入口复用）。 */
void DeepDogMemoryReportAppendTasks(cJSON* root);

/** 串口打印内存与 Top 任务栈（phase 如 boot_baseline / face_ready）。 */
void DeepDogMemoryReportLog(const char* phase);

/** 等待 internal 空闲/最大块达标（毫秒超时）；达标返回 true。 */
bool DeepDogMemoryWaitInternalReady(const char* tag, size_t min_free_bytes, size_t min_largest_bytes,
                                    int timeout_ms);
