#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** WiFi 联网后 SNTP 同步 UTC（幂等）。见 swrs/net/N01-sntp-clock-sync.md */
void DeepDogSntpInit(void);

/** true 当 time(nullptr) >= 1e9（SNTP 或 OTA server_time 已设钟） */
bool DeepDogClockIsSynced(void);

/** 已同步 → Unix 秒；否则 boot 秒（与 face/registry 契约一致） */
uint32_t DeepDogNowUnixSec(void);

typedef void (*DeepDogClockSyncedFn)(void);

/** 首次 SNTP 同步后回调（在 esp_timer 任务上下文，可安全 MQTT publish） */
void DeepDogSntpSetOnSynced(DeepDogClockSyncedFn fn);

#ifdef __cplusplus
}
#endif
