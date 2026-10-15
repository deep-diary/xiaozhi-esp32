# CFG-01 · 统一配置中心（设备端）

| 项 | 内容 |
|----|------|
| ID | CFG-01 |
| 领域 | config（配置中心） |
| 依赖 | [N03 配网页](../net/N03-wifi-portal-mqtt-broker.md) · [M01 MQTT 协议](../mqtt/M01-board-mqtt-protocol.md) · [16 HTTP 配对](../mqtt/modules/16-http-pairing.md) |
| 代码 | `main/boards/deep-dog/config_center/`（新增） |
| 后端 | deep-trace `devices/config/`（Django + DRF） |
| 设计文档 | `deep-trace/docs/design/unified-config-center.md` |
| 状态 | 需求就绪，待实现 |

---

## 1. 目标

将 deep-dog 设备的所有可配置项从「编译期硬编码 + NVS 零散存储」迁移到**统一配置中心管理**：

- 设备开机后从 deep-trace 服务器拉取完整配置，解析生效
- MQTT Broker、MediaMTX 地址、API 服务器等全部从服务器下发
- 每台设备绑定独立配置，支持「模板继承 + 设备级覆盖」
- 双模式同步：MQTT 通知（秒级）+ HTTP 定时轮询（5 分钟降级）
- 高度可配置化，**新增配置项无需重编译固件**

## 2. 范围

### 2.1 本轮落地（CFG-01）

| 模块 | 配置项 | 来源 → 目标 |
|------|--------|------------|
| connectivity.mqtt | broker_host / broker_port / server_api_base / username / password | NVS `deep_dog_mqtt` → 配置中心 |
| connectivity.mediamtx | server_url / push_path_template / srt_passphrase | 编译宏 → 配置中心 |
| config_center | pull_interval_s / mqtt_notify_topic / device_secret | 新增 |
| system | firmware_version / device_name | 只读上报 |

### 2.2 后续迭代（不在本轮）

- 视觉模块配置（fps、分辨率、人脸检测参数）
- 电机/CAN 模块配置
- 舵机/LED 配置
- 音频参数配置
- OTA URL 配置

> 38/48 引出脚模式的运行时可配置已独立成 **[CFG-02](./CFG-02-ext-pin-runtime-mode.md)**（依赖本文档的下发通道 + 重启生效语义）。

## 3. 数据模型（设备端视角）

配置 JSON 顶层结构（与后端 `device.config.v1` schema 对齐）：

```json
{
  "schema_version": "device.config.v1",
  "config_version": 3,
  "fingerprint": "sha256-hex-64chars",
  "issued_at": "2026-10-15T03:45:05Z",
  "ttl_s": 300,
  "config": {
    "connectivity": {
      "mqtt": {
        "host": "broker.example.com",
        "port": 1883,
        "username": "",
        "password": "",
        "use_tls": false,
        "topic_prefix": "deepdog"
      },
      "mediamtx": {
        "server_url": "http://mediamtx.example.com:8880",
        "push_path_template": "/{device_id}/stream",
        "srt_passphrase": "",
        "srt_enabled": false
      },
      "api_server": {
        "base_url": "https://api.example.com/api/v1"
      }
    },
    "config_center": {
      "pull_interval_s": 300,
      "enable_mqtt_notify": true
    },
    "system": {
      "device_name": "deep-dog-001",
      "timezone": "Asia/Shanghai"
    }
  }
}
```

### 3.1 配置合并规则

- 后端做「模板 + 设备覆盖」深度合并，设备端拿到的是**最终配置**
- 设备端不做合并逻辑，只负责解析和应用
- 缺失字段使用固件内置默认值（编译期 fallback）

## 4. 设备端流程

### 4.1 启动流程

```
开机
  ↓
WiFi 连接（配网页 / 已有 WiFi）
  ↓
读取 NVS 缓存配置 → 用默认值填充缺失字段 → 初始化基础模块（MQTT 等）
  ↓
HTTP 拉取最新配置（带本地版本号）
  ├─ 版本相同 → 跳过，保持当前配置
  ├─ 版本不同 → 解析新配置 → 对比差异 → 热更新 / 重启受影响模块
  └─ 失败 → 使用 NVS 缓存，定时重试
  ↓
正常运行：MQTT 监听配置变更通知 + HTTP 定时轮询
```

### 4.2 注册与鉴权

- **设备 ID**：MAC 地址紧凑串（如 `AABBCCDDEEFF`），与现有配对逻辑一致
- **设备密钥**：注册时服务器返回 `device_secret`（32 字节 hex 字符串），存 NVS
- **HMAC-SHA256 签名**：所有配置 API 请求必须带签名头
  - `X-Device-Id`
  - `X-Device-Timestamp`（Unix 秒）
  - `X-Device-Signature`（base64 HMAC-SHA256）
  - Base String: `{method}\n{path}\n{timestamp}\n{body_sha256_hex}`
- **降级**：开发期 `DEVICE_AUTH_REQUIRED=False` 时可跳过签名（服务器端配置）

### 4.3 拉取（Pull）

- **接口**：`POST {api_base}/devices/config/pull`
- **请求体**：
  ```json
  {
    "current_config_version": 0,
    "current_fingerprint": "",
    "schema_versions": ["device.config.v1"],
    "ttl_s": 300
  }
  ```
- **响应**：
  - 版本相同（304 语义）：`{"config_version": N, "ttl_s": 300, "next_poll_s": 300}`
  - 版本不同：完整配置 JSON（见 §3）
- **频率**：默认 5 分钟一次，MQTT 通知触发立即拉取

### 4.4 生效确认（Ack）

- 配置应用完成后，向服务器上报应用结果
- **接口**：`POST {api_base}/devices/config/ack`
- **请求体**：
  ```json
  {
    "config_version": 3,
    "status": "success",
    "errors": []
  }
  ```
- `status`：`success` / `partial` / `failed`
- `errors[]` 元素**固定为对象** `{field, code, message}`（不再是裸字符串）：

  ```json
  { "config_version": 3, "status": "partial", "errors": [
      { "field": "io.ext_pins.mode", "code": "E_UNSUPPORTED_MODE",
        "message": "unsupported ext_pins mode 'spi'" }
  ] }
  ```

  | 字段 | 说明 |
  |------|------|
  | `field` | 配置点分路径，如 `io.ext_pins.mode` |
  | `code` | 设备侧错误码（`E_*`）；与 HTTP 层 `CFG_*` 属不同命名空间 |
  | `message` | 人类可读说明 |

  跨端接口契约真源见 [`docs/interface/03-http-api-contract.md`](../../docs/interface/03-http-api-contract.md)（B. 设备 → 云端配置 API）。
- `status`: `success` / `partial` / `failed`
- 失败时 `errors` 数组包含 `{field, code, message}`

### 4.5 MQTT 通知

- 订阅主题：`{prefix}/{device_id}/cmd/config`
- 消息体：
  ```json
  {
    "action": "refresh",
    "config_version": 5,
    "reason": "template_updated"
  }
  ```
- 收到后立即触发一次 HTTP pull

## 5. 本地存储（NVS）

| 命名空间 | 键 | 类型 | 说明 |
|---------|----|------|------|
| `deep_dog_cfg` | `config_json` | string | 完整配置 JSON（最新生效版本） |
| `deep_dog_cfg` | `config_version` | u32 | 当前配置版本号 |
| `deep_dog_cfg` | `fingerprint` | string | 配置指纹（SHA256 hex） |
| `deep_dog_cfg` | `device_secret` | string | 设备密钥（hex 字符串） |
| `deep_dog_cfg` | `api_base_url` | string | deep-trace API 基址 |
| `deep_dog_cfg` | `last_pull_at` | u64 | 上次成功拉取时间（Unix 秒） |

**迁移策略**：首次启动时，从旧 NVS 命名空间（`deep_dog_mqtt` 等）读取已有配置，作为 override 合并到默认配置中。

## 6. 配置变更分发

设备内部使用**观察者模式**分发配置变更事件：

```
ConfigManager (单例)
  ├─ load_from_nvs()     // 启动时加载
  ├─ pull_from_server()  // HTTP 拉取
  ├─ apply_config()      // 解析 + 对比 + 分发变更
  └─ subscribe(key, callback)  // 订阅配置路径变更

模块订阅示例：
  mqtt_module → subscribe("connectivity.mqtt", on_mqtt_config_changed)
  vision_module → subscribe("connectivity.mediamtx", on_mediamtx_config_changed)
```

- 变更检测基于 JSON 路径对比（只通知实际变更的路径）
- 回调在主任务线程执行，模块自行决定热更新还是标记重启
- 关键配置变更（如 MQTT broker 地址）需要重连，模块自行处理

## 7. 错误处理与降级

| 场景 | 处理策略 |
|------|---------|
| 服务器不可达 | 使用 NVS 缓存配置，指数退避重试（最小 30s，最大 5min） |
| 配置解析失败 | 使用上一版本配置，记录错误日志，上报 ack failed |
| 部分字段无效 | 忽略无效字段，使用默认值，上报 ack partial |
| schema 不兼容 | 使用本地配置，记录 warning，等待固件升级 |
| 签名校验失败 | 开发模式降级放行；生产模式拒绝并报警 |
| MQTT 通知丢失 | HTTP 轮询兜底（5 分钟内生效） |

## 8. 与现有功能的关系

### 8.1 配对流程（DT-02）

- HTTP 配对成功后，服务器返回 `device_id` + `device_secret` + 初始配置
- 设备将密钥和配置写入 NVS，然后启动配置中心
- 配置中心启动后，立即执行第一次 pull，确保拿到最新配置

### 8.2 配网页（N03）

- 配网页中的「MQTT Broker」「服务器 API 地址」迁移为**只读显示**
- 配置变更统一在 deep-trace 管理后台操作
- 配网页保留「重置配置」「手动指定服务器地址」等紧急操作

### 8.3 MQTT 模块

- `DeepDogMqttConfig` 不再直接读 NVS，改为从 `ConfigManager` 获取
- 配置变更时，`DeepDogMqtt` 自动重连到新 broker

## 9. 验收标准

### 9.1 功能验收

- [ ] 设备开机后能从服务器拉取配置并正确解析
- [ ] MQTT 配置变更后设备自动重连到新 broker
- [ ] MediaMTX 配置变更后推流地址自动更新
- [ ] MQTT 配置变更通知能触发设备立即拉取
- [ ] HTTP 轮询兜底机制正常（验证 MQTT 断连时仍能同步配置）
- [ ] 服务器不可达时使用 NVS 缓存正常运行
- [ ] 配置版本相同时返回 304 简化响应，节省流量
- [ ] HMAC 签名鉴权正常（生产模式）

### 9.2 非功能验收

- [ ] 配置拉取 + 解析总耗时 < 500ms（不含网络）
- [ ] 内存占用增加 < 10KB（配置 JSON + 管理器）
- [ ] 所有配置变更有日志记录（`ESP_LOGI` / `ESP_LOGE`）
- [ ] NVS 读写异常有降级处理，不导致 crash

## 10. 风险与注意事项

1. **设备密钥安全**：`device_secret` 存 NVS，生产环境需考虑加密存储（后续迭代）
2. **配置兼容性**：新增字段必须可选，旧固件忽略未知字段
3. **回滚能力**：支持回滚到上一版本配置（服务器端版本历史）
4. **批量下发风暴**：大量设备同时拉取可能导致服务器压力，由后端做错峰
5. **首次启动**：无网络无缓存时使用编译期默认值，确保设备可启动

---

## 11. 文件清单（待实现）

```
main/boards/deep-dog/config_center/
├── config_types.h          // 配置结构体定义
├── config_manager.h        // 配置管理器（单例）
├── config_manager.cc
├── config_api_client.h     // HTTP API 客户端
├── config_api_client.cc
├── config_change_hub.h     // 变更事件分发
├── config_change_hub.cc
└── Kconfig                 // 配置中心相关 menuconfig 选项
```

接入点：
- `DeepDogMqtt::Start` → 从 ConfigManager 读取 MQTT 配置
- `DeepDogPairHttp` → 配对成功后写入 ConfigManager
- `esp_sparkbot_board.cc` → 启动流程中初始化 ConfigManager
