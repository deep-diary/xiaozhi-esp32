# N01 · SNTP 时钟同步

| 项 | 内容 |
|----|------|
| module_id | `net` / `device`（状态上报） |
| 契约 | ready |
| 依赖 | WiFi STA 获 IP |
| 关联 | [01-device](../mqtt/modules/01-device.md) · [04-face](../mqtt/modules/04-face.md) · [S05 Immich 时间轴](../vision/server/S05-immich-real-name.md) |

## 目标

WiFi 联网后自动 SNTP 同步 UTC，使 `time()` 为可信 Unix 秒（≥ `1e9`），支撑：

- `face/registry` 的 `last_seen_at` / `updated_at`
- Immich `fileCreatedAt` / `fileModifiedAt`
- `device/status` / `device/info` 的 `ts` / `ts_iso`
- 主动招呼 `greet_gap_sec` 间隔判断

## 边界

| 来源 | 行为 |
|------|------|
| **SNTP（本需求）** | WiFi 获 IP 后 `esp_netif_sntp_init`；主/备 NTP 见 `net_config.h` |
| **OTA `server_time`** | 保留；若 OTA 先设钟，SNTP 仍运行以维持漂移 |
| **未同步** | `TouchLastSeen` 跳过写入；`last_seen_at` 保持 0 或历史有效值 |

## 配置（`net/net_config.h`）

| 宏 | 默认 | 说明 |
|----|------|------|
| `DEEP_DOG_SNTP_ENABLE` | `1` | 置 0 关闭 SNTP（仅 OTA/手动设钟） |
| `DEEP_DOG_SNTP_SERVER_0` | `pool.ntp.org` | 主 NTP |
| `DEEP_DOG_SNTP_SERVER_1` | `ntp.aliyun.com` | 备 NTP（国内 LAN） |

## API（固件 `net/deep_dog_sntp.h`）

| 函数 | 说明 |
|------|------|
| `DeepDogSntpInit()` | 幂等；在 `StartNetwork` 调用 |
| `DeepDogClockIsSynced()` | `time(nullptr) >= 1e9` |
| `DeepDogNowUnixSec()` | 已同步 → Unix 秒；否则 boot 秒 |
| `DeepDogSntpSetOnSynced(fn)` | 首次同步回调（MQTT 侧重发 registry/status） |

## MQTT · `device/status` 增字段

| 字段 | 类型 | 说明 |
|------|------|------|
| `clock_synced` | bool | `DeepDogClockIsSynced()` |

未同步时 `health.warn` 可含 `clock_unsynced`（WiFi 已连且 MQTT 在线时）。

## 验收

- [ ] WiFi 获 IP 后 30s 内串口出现 `SNTP synced` 日志
- [ ] `device/status.clock_synced=true`；`ts` 与浏览器 UTC 偏差 &lt; 5s
- [ ] 识别熟人后 `face/registry` 出现有效 `last_seen_at`（≥1e9）
- [ ] SNTP 同步后 retain `face/registry` 重发一次（便于 Web/Django 镜像）
- [ ] `DEEP_DOG_SNTP_ENABLE=0` 时行为与改前一致（依赖 OTA 设钟）
