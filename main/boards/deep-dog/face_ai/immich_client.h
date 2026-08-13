#pragma once

#include "face_ai_config.h"

#include <cstddef>
#include <cstdint>

/** Immich 真名客户端（S05）：配置存 NVS；异步 upload→poll people；可选 delete。 */

bool DeepDogImmichInit();
void DeepDogImmichDeinit();

/** dog_immich 任务是否已创建（upload/poll 依赖此项） */
bool DeepDogImmichIsWorkerReady();

bool DeepDogImmichIsConfigured();
/**
 * 写入 NVS。api_url 可空则用默认局域网地址。
 * delete_asset：<0 不改；0/1 写入「识别后是否删临时 asset」。
 * 若 api_key 为空但已配置过 Key，仍可只更新 delete_asset。
 * 禁止把 Key 打进完整日志。
 */
bool DeepDogImmichSetConfig(const char* api_url, const char* api_key, int delete_asset = -1,
                            const double* latitude = nullptr, const double* longitude = nullptr);

/** NVS 无 Key 时应用编译期默认 URL/Key（若有）并写入 NVS。必须在 internal 栈调用（会读/写 Flash）。 */
void DeepDogImmichApplyDefaultsIfEmpty();

/**
 * 启动前在 internal 栈加载 Immich NVS 配置（RuntimeStart / immich_late / MQTT）。
 * dog_face_ai（PSRAM 栈）与 dog_immich worker 均不得直接触发 NVS/Flash。
 */
void DeepDogImmichPrepareConfig();

/** GET {url}/server/ping；更新内部 server_ok/ping_ms 并触发 status 回调。 */
bool DeepDogImmichPingServer();

using DeepDogImmichStatusChangedFn = void (*)();
void DeepDogImmichSetStatusChangedCallback(DeepDogImmichStatusChangedFn cb);

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
/** 对 NVS 中已存 asset_id 立即 poll（无 upload）。 */
bool DeepDogImmichPollStoredAsset(int local_id);
