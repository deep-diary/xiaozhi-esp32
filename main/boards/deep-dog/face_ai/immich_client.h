#pragma once

#include "face_ai_config.h"

#include <cstddef>
#include <cstdint>

/** Immich 真名客户端（S05）：配置存 NVS；异步 upload→poll people→delete。 */

bool DeepDogImmichInit();
void DeepDogImmichDeinit();

bool DeepDogImmichIsConfigured();
/** 写入 NVS；api_url 可空则用默认局域网地址。禁止把 Key 打进完整日志。 */
bool DeepDogImmichSetConfig(const char* api_url, const char* api_key);

/** 状态 JSON（无 Key 明文），写入 buf。 */
size_t DeepDogImmichFormatStatusJson(char* buf, size_t buf_size);

/**
 * 请求为 local_id 取真名。接管 jpeg（heap_caps_free）。
 * force=true 时忽略「已有真名」。队列满或未配置则释放 jpeg 并返回 false。
 */
bool DeepDogImmichRequestName(int local_id, uint8_t* jpeg, size_t jpeg_len, bool force);

/** 标记下次出镜时强制刷新该 local_id（0=当前 primary，由 runtime 填）。 */
void DeepDogImmichRequestRefresh(int local_id);
bool DeepDogImmichConsumeForceRefresh(int local_id);
