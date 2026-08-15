# 01 · device（设备信息）

| 项 | 内容 |
|----|------|
| module_id | `device` |
| capabilities | （核心模块，随客户端上线；页头始终可用） |
| 路由建议 | `/device/:deviceId/modules/device`（可选；也可仅用设备页页头） |
| 契约 | ready（字段已定） |
| YAML | `topics.device/info`、`device/status` |
| 路线图 | 随 MQTT 客户端 |

## 入口卡文案

- 标题：设备信息  
- 说明：固件、IP、能力列表、内存与系统健康  

## 详情页目标

展示完整 `device/info` 与心跳 `device/status`；无下行 cmd（v0.1）。

## 边界

| 归属 | Topic | 内容 |
|------|-------|------|
| 静态/半静态元数据 | `device/info`（retain） | 固件、芯片、能力、复位原因、电量能力位 |
| 运行时心跳 | `device/status` | 在线、内存分项、WiFi、系统级 `health.warn` |
| 电机 / 姿态故障 | [`dog/status`](./07-dog.md) | `has_fault`、电机明细；**不**并入 `device/status` |

电量：当前板级无 ADC/PMIC，`power.supported=false`，不发伪数值。未来有采样时再发 `level_pct` / `charging`。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `device/info` | ↑ | 0 | true |
| `device/status` | ↑ | 0 | false |

前缀：`deepdiary/deep-dog/{device_id}/`。字段以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

**`device_id` 语义**：生产默认 = STA MAC 紧凑串（小写无冒号）；与 Topic 前缀一致。添加设备 / 配对见 [00-pairing](./00-pairing.md)，勿要求用户手输本字段。

## 字段表 · `device/info`

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `device_id` | string | 是 | 与 Topic 前缀相同；默认 MAC 紧凑 |
| `firmware` | string | 是 | `esp_app_desc.version` |
| `board` | string | 否 | 板名，如 `deep-dog` |
| `chip_model` | string | 否 | 如 `esp32s3` |
| `idf_version` | string | 否 | IDF 版本串 |
| `flash_size` | int | 否 | Flash 字节数 |
| `mac` | string | 否 | STA MAC，`AA:BB:…` |
| `ip` | string | 否 | 当前 IPv4 |
| `http_port` | int | 否 | HTTP 服务端口 |
| `reset_reason` | string | 否 | `poweron` / `external` / `software` / `panic` / `watchdog` / `deepsleep` / `brownout` / `sdio` / `unknown` |
| `power.supported` | bool | 否 | 当前固定 `false` |
| `power.level_pct` | int | 否 | 仅 `supported=true` 时 |
| `power.charging` | bool | 否 | 仅 `supported=true` 时 |
| `ext_pins.mode` | string | 否 | 自由引出脚成对模式：`none\|can\|uart\|rs485\|pwm\|io\|ad\|led` |
| `ext_pins.gpio_a` / `gpio_b` | int | 否 | 默认 38 / 48 |
| `capabilities.*` | bool | 是 | 与 `DEEP_DOG_*_ENABLE` 对齐（含 `motor`/`arm`/`uart`） |
| `ts` | int | 是 | Unix 秒 |
| `ts_iso` | string | 否 | UTC ISO8601 |

WiFi SSID/信道放在 **status**（可漫游），不写入 retain info。

## 字段表 · `device/status`

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `online` | bool | 是 | 心跳时恒为 `true` |
| `uptime_s` | int | 否 | 启动以来秒数 |
| `free_heap` | int | 否 | `esp_get_free_heap_size`（字节） |
| `min_free_heap` | int | 否 | 历史最小空闲堆 |
| `mem.internal.{free,min,total,largest_free,used}` | int | 否 | 内部 SRAM heap 池，字节 |
| `mem.psram.{free,min,total,largest_free,used}` | int | 否 | 外部 PSRAM；无则 `total=0` |
| `rssi` | int | 否 | dBm |
| `wifi_ssid` | string | 否 | 当前 AP |
| `wifi_channel` | int | 否 | 信道 |
| `health.ok` | bool | 否 | 无系统级告警时为 `true` |
| `health.warn` | string[] | 否 | 如 `low_internal_heap`（internal free &lt; 32KiB）、`clock_unsynced`（WiFi 已连但 SNTP/OTA 未设钟） |
| `clock_synced` | bool | 否 | `time()` 为可信 Unix 秒（≥1e9）；见 [N01 SNTP](../../net/N01-sntp-clock-sync.md) |
| `task_count` | int | 否 | 当前 FreeRTOS 任务数（需 `CONFIG_FREERTOS_USE_TRACE_FACILITY`） |
| `tasks[]` | array | 否 | `{ name, prio, stack_hwm, state, stack_domain?, stack_used_est? }`；按 `stack_hwm` **升序**（余量最小=最紧张在前） |
| `ts` / `ts_iso` | int / string | 是 / 否 | 时间戳 |

## 样例 JSON · `device/info`

```json
{
  "device_id": "aabbccddeeff",
  "firmware": "2.1.0",
  "board": "deep-dog",
  "chip_model": "esp32s3",
  "idf_version": "v5.4.1",
  "flash_size": 16777216,
  "mac": "AA:BB:CC:DD:EE:FF",
  "ip": "192.168.31.211",
  "http_port": 8080,
  "reset_reason": "poweron",
  "power": { "supported": false },
  "ext_pins": { "mode": "can", "gpio_a": 38, "gpio_b": 48 },
  "capabilities": {
    "dog": false, "motor": true, "stream": true, "face": true, "track": true, "imu": true,
    "led": false, "servo": false, "gimbal": false,
    "handle": true, "touch": true, "can": true, "arm": false, "uart": false
  },
  "ts": 1710000000,
  "ts_iso": "2024-03-09T12:00:00Z"
}
```

联调覆盖 `device_id=dev` 时，样例中的 `device_id` 与 Topic 前缀改为 `dev` 即可。

## 样例 JSON · `device/status`

```json
{
  "online": true,
  "uptime_s": 3600,
  "free_heap": 185000,
  "min_free_heap": 120000,
  "mem": {
    "internal": { "free": 98000, "min": 72000, "total": 327680, "largest_free": 45000, "used": 229680 },
    "psram": { "free": 6200000, "min": 5100000, "total": 8388608, "largest_free": 6000000, "used": 2188608 }
  },
  "rssi": -57,
  "wifi_ssid": "HomeWiFi",
  "wifi_channel": 6,
  "health": { "ok": true, "warn": [] },
  "clock_synced": true,
  "ts": 1710000005,
  "ts_iso": "2024-03-09T12:00:05Z"
}
```

低内存告警样例：

```json
{
  "online": true,
  "uptime_s": 7200,
  "free_heap": 42000,
  "min_free_heap": 28000,
  "mem": {
    "internal": { "free": 24000, "min": 18000, "total": 327680, "largest_free": 12000, "used": 303680 },
    "psram": { "free": 4100000, "min": 3900000, "total": 8388608, "largest_free": 3800000, "used": 4288608 }
  },
  "rssi": -72,
  "wifi_ssid": "HomeWiFi",
  "wifi_channel": 6,
  "health": { "ok": false, "warn": ["low_internal_heap"] },
  "ts": 1710003600,
  "ts_iso": "2024-03-09T13:00:00Z"
}
```

## Steps（前端）

- **Step 1** 进入页（或设备页页头复用同一数据源）。
- **Step 2** 确保已订阅 `device/info`（可与设备页共享连接状态）。
- **Step 3** 渲染字段表 + capabilities 开关一览（只读）；展示 `power.supported`（当前为不支持电量）。
- **Step 4** 订阅 `device/status`：显示 online/uptime、RSSI、`mem` 摘要、`health.ok` / `warn`。
- **Step 5** 电机故障勿从本 Topic 推断；有 `capabilities.dog` 时看 [`07-dog`](./07-dog.md)。
- **Step 6** 离页时若本页独占订阅则退订。

## 固件实现

- 上线/重连后 publish `device/info`（retain）。
- 周期 publish `device/status`（建议 ~0.2 Hz，现 5s）。
- `capabilities.*` 与编译宏 `DEEP_DOG_*_ENABLE` 对齐。
- `ext_pins` 反映 `config.h` 中 `DEEP_DOG_EXT_PIN_MODE`（成对引出脚）；前端用 `mode` 切换 can/uart/pwm/led 等页面，用 `capabilities.motor` vs `dog` 区分单电机与四足；`mode=led` 时 DIN=`gpio_a`，须 `capabilities.led` 才出灯带卡。
- `mem.*` 用 `heap_caps_get_*`；`health.warn` 含 `low_internal_heap` 当 internal free &lt; 32768。
- `clock_synced`：`DeepDogClockIsSynced()`；WiFi 已连且未同步时 `health.warn` 含 `clock_unsynced`。SNTP 见 [N01](../../net/N01-sntp-clock-sync.md)。
- `power`：当前固定 `{ "supported": false }`。

## 验收

- [ ] 前端能展示 capabilities 并驱动入口卡显隐
- [ ] retain 晚订阅仍能拿到 info
- [ ] status 能展示内存分项与 health
- [ ] 样例字段与 YAML / 固件一致
