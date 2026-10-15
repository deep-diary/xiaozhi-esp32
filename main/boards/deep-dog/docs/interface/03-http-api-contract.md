# 03 · HTTP / REST 标准接口契约

> 契约版本：`interop-1`（2026-10）
> 关联：[01-mqtt-contract.md](./01-mqtt-contract.md) · [02-ws-contract.md](./02-ws-contract.md)

本文覆盖三类 HTTP 面：
- **A. 设备内置 httpd**：设备对外提供（局域网）。
- **B. 设备 → 云端配对 HTTP**：设备主动调用云端。
- **C. 云端 REST API**：前端/第三方调用 Django（`/api/v1/`）。

---

## A. 设备内置 httpd

- 开关：`DEEP_DOG_HTTP_SERVER_ENABLE`（默认 **0**，`http_server_config.h:7-9`）；端口 `8080`（`http_server_config.h:12-14`）。
- CORS：所有 JSON 响应带 `Access-Control-Allow-Origin: *` 且禁缓存（`deep_dog_http_server.cc:121-129`）。
- 状态：该模块**将逐步弃用**，UI 迁移至 deep-trace 前端（`requirements/ws-mcp/01:64-69`）。

| Method | Path | 用途 | 关键参数 | 响应 |
|--------|------|------|----------|------|
| GET | `/` | 内置遥控页 | — | text/html |
| GET | `/stream` | 局域网 MJPEG | — | multipart/x-mixed-replace |
| GET | `/api/status` | 采集/推流状态 | — | JSON（push/play URL、mode） |
| POST | `/api/capture_mode` | 设采集模式 | `?mode=off\|periodic\|stream\|rtsp_push` | `{ok,mode}` |
| POST | `/api/vision_publish` | 同 `capture_mode`（别名） | `?mode=` | JSON |
| POST | `/api/cmd` | 狗动作 | `?cmd=init\|forward\|back\|stand\|liedown\|dance\|stop_walk\|disable` | `{ok}` |
| GET | `/api/dog_status` | 电机角度/扭矩/碰撞 | — | `{motors[],torque_limit_nm,has_fault}` |
| GET | `/api/face` | 人脸检测快照 | — | JSON |
| POST | `/api/face_enable` | 开关人脸检测 | `?enabled=0\|1` | `{ok}` |
| POST | `/api/immich_config` | Immich 配置 | `?api_key=&api_url=&delete_asset=` | `{ok}` |
| GET | `/api/immich_status` | Immich 状态 | — | JSON |
| POST | `/api/face_refresh_name` | 刷新某人 Immich 姓名 | `?local_id=` | `{ok,local_id}` |

出处：`http-server/deep_dog_http_server.cc:1065-1091`（路由注册）。

> **约定**：`/api/status` 与 MQTT `{p}/stream/status`、`/api/face` 与 `{p}/face/status` 为**同源数据**（HTTP 供局域网直连，MQTT 供云端）。

---

## B. 设备 → 云端配对 HTTP

设备侧实现：`pairing/deep_dog_pair_http.cc`；云端实现：`backend/devices/views.py:159-211`、`devices/services.py`。
请求头统一：`Content-Type: application/json`、`Device-Id: <MAC 紧凑串>`（`deep_dog_pair_http.cc:107-110`）。

> **契约决议（D4）**：配对**上行**（握手/确认/解绑）**全部走 HTTP**；云端**移除** MQTT 配对 ingest
> （不再订阅 `pairing/status`、`pairing/request`）。配对状态**单一来源 = HTTP**，MQTT 仅保留**下行**通知
> `{prefix}/{device_id}/cmd/pairing`。详见 [01-mqtt-contract.md §5.6/§6](./01-mqtt-contract.md)。

### B.1 握手

```
POST {server_api_base}/devices/pair/handshake
Body: { "mac": "AABBCCDDEEFF", "fw": "1.2.3" }
```

| 场景 | 状态码 | 响应体 |
|------|--------|--------|
| 未绑定 | 200/202 | `{ code, device_id, ... }`（6 位配对码） |
| 已绑定 | 200 | `{ bound: true, device_id, mqtt: {...}, topics: {...} }` |
| 失败 | 4xx | `{ detail }` |

### B.2 确认

```
POST {server_api_base}/devices/pair/activate
Body: { "device_id": "...", "challenge"?: "...", "hmac"?: "..." }
```

| 场景 | 状态码 |
|------|--------|
| 未绑定（继续轮询） | 202 |
| 已绑定（应用配置） | 200 |
| 过期/无效 | 404 / 410 |
| 鉴权失败 | 401 |
| 限流 | 429（退避重试） |

设备状态机见 `pairing/deep_dog_pair_http.cc:394-434`；绑定结果写 NVS 并 defer 重载。

### B.3 解绑（新增，替代 MQTT `pairing/request`）

> 现状：设备主动解绑走 MQTT `pairing/request` → ingest（[services.py:471-512](file:///D:/working/test-platform/deep-trace-com/backend/devices/services.py#L471-L512)）。
> 决议 D4：改为设备面 HTTP 接口，MQTT 该上行通道退役。

```
POST {server_api_base}/devices/pair/unbind
Header: Device-Id: <MAC 紧凑串>
Body: { "device_id": "...", "reason"?: "user" }
```

| 场景 | 状态码 | 响应体 |
|------|--------|--------|
| 解绑成功 | 200 | `{ ok: true, device_id, db_unbound, mqtt_published }` |
| 设备不在库 | 200 | `{ ok: true, db_unbound: false, note: "…NVS sync…" }`（仍下发 `cmd/pairing unbind` 清设备 NVS） |
| 参数非法 | 400 | `{ detail }` |

云端处理：清 `Device.work_unit`/`bound_at` → 发 MQTT `{prefix}/{mac}/cmd/pairing {action:"unbind"}` 通知设备清 NVS。

### B.4 安全（待改善）

- 云端 `/devices/pair/*` **无用户鉴权**（设备面，`views.py:165-166`），HMAC 默认空即放行（`devices/services.py:524-532`）。
- **建议**：强制 challenge/HMAC 校验（含 `unbind`）；对 429 退避参数与配对码 TTL 做契约化说明。

---

## C. 云端 REST API

### C.1 全局规范

| 项 | 规范 | 出处 |
|----|------|------|
| 前缀 | `/api/v1/` | `backend/config/urls.py:18-26` |
| 鉴权 | `TokenAuthentication`；默认 `AllowAny`，各视图覆盖 | `config/settings/base.py:87-90` |
| 认证头 | `Authorization: Token <token>` | `frontend/src/api/client.js:8-11` |
| 文档 | `api/schema/`、`api/docs/`（OpenAPI / drf-spectacular） | `config/urls.py:16-17` |
| 时间格式 | **建议统一 ISO8601 带时区**；REST 由 DRF 序列化决定 | — |
| 分页 | DRF 默认分页（各 ViewSet） | — |

### C.2 设备 / 配对（`devices/urls.py`）

| Method | Path | 鉴权 | 用途 |
|--------|------|------|------|
| GET/POST | `/devices` | 有 | 设备清单 / 添加 |
| PATCH/DELETE | `/devices/{code}` | 有 | 编辑 / 软删 |
| POST | `/devices/pair/handshake` | **无**（设备面） | 配对握手 |
| POST | `/devices/pair/activate` | **无**（设备面） | 配对确认 |
| GET | `/homes/{home_code}/devices` | 有 | 家居设备列表 |
| POST | `/homes/{home_code}/devices/bind` | 有 | 配对码绑定 |
| POST | `/homes/{home_code}/devices/add` | 有 | 按 UUID 添加 |
| PATCH | `/homes/{home_code}/devices/{device_id}` | 有 | 改名 |
| POST | `/homes/{home_code}/devices/{device_id}/unbind` | 有 | 解绑 |
| GET | `/homes/{home_code}/devices/{device_id}/faces` | 有 | 人脸库镜像 |
| DELETE | `.../faces/{local_id}` | 有 | 删单条人脸 |
| GET/POST | `.../modbus/slaves` | 有 | Modbus 从站 |
| GET/PUT/PATCH | `.../modbus/slaves/{pk}/mapping` | 有 | 从站映射 |
| GET/POST | `.../modbus/mappings` | 有 | mapping 列表/上传 |

### C.3 遥测 / FCT（`telemetry/urls.py`）

| Method | Path | 鉴权 | 用途 |
|--------|------|------|------|
| GET/POST | `/telemetry/samples` | 有 | 历史列表 / 边缘实时推送 |
| GET | `/telemetry/series` | 有 | 时序（可抽样） |
| GET | `/telemetry/stats` | 有 | 区间统计 |
| GET/POST | `/fct/test-results` | 有 | 查询 / 入库（`dtp.test.result.*`） |
| GET | `/fct/test-result-facets` | 有 | 归类角标 |

### C.4 边缘（`edge/urls.py`，**全部无鉴权设备面**）

| Method | Path | 用途 |
|--------|------|------|
| GET | `/edge/config` | 下发网关 slaves/mappings 配置 |
| POST | `/edge/regs-snapshot` | Modbus 寄存器快照 → 过站入库 |
| POST | `/edge/nodes/register` | 边缘节点注册（幂等） |
| POST | `/edge/nodes/heartbeat` | 节点心跳 |

### C.5 配置中心（`devices/urls.py` → `devices/config/urls.py`，CFG-01）

> 后端实现：`deep-trace/backend/devices/config/`；契约与需求见 [CFG-01](../requirements/config/CFG-01-unified-config-center.md)。
> 设备面接口用 **HMAC 设备自签名**（非 Token），管理面用 Token。

**设备面**（HMAC；开发期 `DEVICE_AUTH_REQUIRED=False` 可放行）

| Method | Path | 用途 |
|--------|------|------|
| POST | `/devices/register` | 首次注册，返回 `device_secret`（无鉴权） |
| POST | `/devices/config/pull` | 拉取配置（携带本地版本号） |
| POST | `/devices/config/ack` | 配置生效确认 |

签名规则（`devices/config/auth.py`）：

```
Header: X-Device-Id / X-Device-Timestamp / X-Device-Signature(base64)
Base  : "{METHOD}\n{path}\n{timestamp}\n{body_sha256_hex}"
Key   : hex_decode(device_secret)
算法   : HMAC-SHA256
```

`pull` 请求/响应：

```jsonc
// 请求
{ "current_config_version": 0, "current_fingerprint": "", "schema_versions": ["device.config.v1"], "ttl_s": 300 }
// 200 变更
{ "schema_version": "device.config.v1", "config_version": 1, "fingerprint": "<64hex>",
  "issued_at": "...", "ttl_s": 300, "config": { /* connectivity/modules/features/runtime */ } }
// 200 未变更（304 语义，简化体）
{ "config_version": 1, "ttl_s": 300, "next_poll_s": 300 }
```

`ack` 请求（生效确认）：

```jsonc
// 全部生效
{ "config_version": 1, "status": "success", "errors": [] }
// 部分生效：字段被拒绝 / 忽略，设备已回退到默认值或上一值
{ "config_version": 2, "status": "partial", "errors": [
    { "field": "io.ext_pins.mode", "code": "E_UNSUPPORTED_MODE",
      "message": "unsupported ext_pins mode 'spi'" }
] }
```

- `status`：`success` | `partial` | `failed`
- `errors[]` 元素**固定为** `{field, code, message}`：
  - `field`：配置点分路径（如 `io.ext_pins.mode`）
  - `code`：设备侧错误码（`E_*`），与 HTTP 层 `CFG_*` 分属两个命名空间
  - `message`：人类可读说明
- `E_*` 现有取值（设备端已实现）：`E_UNSUPPORTED_MODE`（不在白名单）· `E_RO_PIN`（只读字段被修改，已忽略）· `E_APPLY_FAILED`（重启后初始化失败，已自动回滚）；`E_MODE_CONFLICT`（模式互斥）为预留码
- **不要**再用裸字符串数组（`["io.ext_pins.mode:E_UNSUPPORTED_MODE"]`）；后端对未知形状应容错并记录告警

**管理面**（Token）

| Method | Path | 用途 |
|--------|------|------|
| GET/POST | `/config/templates/` | 模板列表 / 新建 |
| GET/PUT/PATCH/DELETE | `/config/templates/{pk}` | 模板详情 / 更新 / 删除 |
| GET | `/config/devices/` | 设备配置列表 |
| GET/PATCH | `/config/devices/{device_id}/` | 设备配置详情 / 覆盖 |
| GET | `/config/devices/{device_id}/versions` | 版本历史 |
| POST | `/config/devices/{device_id}/rollback` | 回滚 |
| POST | `/config/bulk/push` | 批量下发 |

**错误码**：`CFG_001` 网络超时 · `CFG_002` schema 不兼容 · `CFG_003` 指纹错误 · `CFG_004` 字段校验失败 · `CFG_005` 设备未注册 · `CFG_006` 鉴权失败 · `CFG_007` 注册失败

> ⚠️ **待确认（接口审计）**：现有文档 B 段示例指向 `deep-trace-com`，而 CFG-01 实现落在 `deep-trace`。
> 需与团队确认设备 `server_api_base` 最终指向哪个后端仓库，避免双仓分裂。

### C.6 其它

- **organization**：`groups` / `enterprises` / `areas` / `process-segments` / `virtual-lines` / `production-lines` / `work-units` / `line-work-units` / `products`（Router），自定义 action：`work-units/{code}/cmd`（→ 发布 `deep-trace/{uuid}/cmd/{module}`）、`modules`、`kpi`、`oee` 等（`organization/urls.py:17-30`、`views.py:1198`）。
- **metadata**：`data-types`、`work-unit-bindings`、`config/test-points`、`customers`、`config/module-specs` 等（`metadata/urls.py:17-34`）。
- **trace**：`trace/units/resolve`、`trace/events/submit`、`trace/search`、`trace/stats/*`、`packaging/*`（`trace/urls.py:36-60`）。
- **accounts**：`auth/login`（AllowAny）、`auth/me`、`auth/change-password`、`auth/my-lines`（`accounts/urls.py:12-16`）。
- **ai**：`ai/chat`、`ai/health`、`ai/stt`、`ai/8d/*`（⚠️ 当前无显式权限，默认 `AllowAny`，见待改善）。
- **graph**：`graph/overview`、`graph/search`、`graph/facets`（IsAuthenticated）。

---

## D. 统一响应与错误码（建议）

### D.1 成功响应

```jsonc
{ "code": 0, "message": "ok", "data": { } }        // 可选包装，或直接返回业务对象
```

> 现状：各 view 直接返回业务对象/`{detail}`，未统一外壳。**建议**：新接口统一 `{code,message,data}`；旧接口保持并登记。

### D.2 错误响应

```jsonc
{ "code": 1001, "message": "参数缺失", "detail": "field X required" }
```

错误码沿用 [01-mqtt-contract.md §5.3](./01-mqtt-contract.md#53-统一错误码体系建议) 分段，与 MQTT `ack.code` 保持同一体系。

### D.3 HTTP 状态码约定

| 码 | 含义 |
|----|------|
| 200 | 成功 |
| 201 | 创建成功 |
| 202 | 已接受（配对未完成/异步） |
| 400 | 请求错误 |
| 401/403 | 鉴权 / 授权失败 |
| 404/410 | 不存在 / 已失效（配对码过期） |
| 429 | 限流（退避重试） |
| 5xx | 服务端错误 |

---

## E. 版本策略

- 前缀版本：`/api/v1/`（破坏性变更升 `v2`，`v1` 保留弃用期）。
- 兼容：新增字段只增不改；删除/改名走弃用期并与 [01 §8](./01-mqtt-contract.md#8-版本兼容规则) 同步。
- 契约变更流程见 [README.md](./README.md#契约变更流程双方约定)。

---

## F. 待改善

- [ ] **新增设备面解绑接口 `POST /devices/pair/unbind`**（替代 MQTT `pairing/request`，见 §B.3）。
- [ ] **移除配对 MQTT ingest**（`ALL_CHANNELS` 去 `pairing`，`PairingMqttIngest` 退役）。
- [ ] 设备面 `/devices/pair/*`、`/edge/*` 补鉴权/签名校验（现全开放）。
- [ ] AI 视图显式声明 `permission_classes`（现默认 AllowAny）。
- [ ] 统一响应外壳与错误码（§D）。
- [ ] 统一三套设备标识键：`home_code` / `production_line_code` / `station_id`（UDA 称以 `production_line_code` 为规范名）。
- [ ] 内置 httpd 弃用计划与前端迁移路径登记。
