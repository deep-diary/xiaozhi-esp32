# 之家 · 按配对码添加设备（前端）

| 项 | 内容 |
|----|------|
| 读者 | **前端** |
| 路由 | `/homes/:homeCode/devices` |
| 依赖 | [00-pairing](../modules/00-pairing.md)、REQ-IOT-142、REQ-IOT-143 |
| 契约 | 展示与导航；字段以 [YAML](../protocol/deep-dog-mqtt.yml) 为准 |

## 目标

用户在「之家设备列表」通过设备播报的 **6 位配对码** 添加设备，不手输 MAC / `device_id`。绑定经 Django HTTP API；**不**在浏览器直连 MQTT 完成绑定。

**前置**：须先在设备上进入配对（语音「添加设备」/ MCP `self.device.start_pairing`，或 **长按键1 + 轻触键2**），设备屏显并播报 6 位码后，再在网页输入。

## 信息架构

| 层级 | 职责 |
|------|------|
| **本页** | 设备列表、添加对话框、解绑入口 |
| **Hub** | 绑定成功后进入；见 [00-device-page](./00-device-page.md) |
| **Pairing** | 无独立 Hub 卡；MQTT `pairing/*` 由后端 worker 消费 |

## 主路径 · 配对码添加

| 项 | 约定 |
|----|------|
| 入口 | 页头「添加设备」 |
| 输入 | 6 位数字配对码（必填）；可选显示名 |
| API | `POST /api/v1/homes/{homeCode}/devices/bind`，body `{ code, name? }` |
| 成功 | 列表出现设备；标签「已配对」；`edge._server_bound=true`；进入 Hub |
| Topic | 前缀 `deepdiary/deep-dog/{device_id}/`；`device_id` 为 API 返回的 MAC 紧凑串 |
| Broker | `wss://mqtt-ws.deep-diary.com/mqtt` |

## 调试路径 · 本地 device_id（REQ-IOT-140）

| 项 | 约定 |
|----|------|
| 入口 | 添加对话框勾选「高级：按 device_id 本地添加（调试）」 |
| 落库 | `localStorage`（`deep-trace:home-devices:${homeCode}`） |
| 标签 | 「本地绑定」；`edge._local_bound=true` |
| 删除 | 仅本地移除，不调后端 |

## 解绑 / 删除

| 入口 | 行为 |
|------|------|
| 设备列表卡片 | 服务端绑定 →「解绑」；本地绑定 →「删除」 |
| Hub 页头 | 「删除 / 解绑」；服务端调 `POST …/devices/{device_id}/unbind` 后回列表 |

解绑后后端下发 `pairing/cmd` `action=unbind`；设备可再次配对。

设备侧也可 **长按键1 + 轻触键3** 或语音「解绑」触发 `pairing/request`，由 ingest 自动解绑。

## 错误展示

| HTTP | 用户可见文案（优先于通用 message） |
|------|-----------------------------------|
| 404 | 配对码无效或设备未上报（请先在设备上进入配对模式） |
| 410 | 配对码已过期，请让设备重新播报 |
| 409 | 设备已绑定到其他之家 |
| 其他 | API `detail` 或「添加失败」 |

## Steps（前端）

1. 进入 `/homes/:homeCode/devices`；`fetchMyLines` + `fetchHomeDevices` 合并 seed / 服务端 / 本地列表。
2. 点击「添加设备」→ 输入 6 位码（`inputmode="numeric"`，仅数字）→「配对绑定」。
3. 成功 → toast；跳转 `/device/:homeCode/:workUnitCode` Hub。
4. Hub **不**订阅 `pairing/*`；仅用 `device/info`、`device/status`。

## 落地代码

| 路径 | 说明 |
|------|------|
| `/Volumes/MacExtStorage/projects/deep-trace/frontend/src/views/iot/HomeDeviceSelectView.vue` | 列表 + 添加/解绑对话框 |
| `/Volumes/MacExtStorage/projects/deep-trace/frontend/src/iot/homeDevices.js` | 本地/服务端合并、`buildServerBoundWorkUnit` |
| `/Volumes/MacExtStorage/projects/deep-trace/frontend/src/views/iot/components/DeviceHubPanel.vue` | Hub 解绑 |
| `/Volumes/MacExtStorage/projects/deep-trace/frontend/src/api/client.js` | `bindHomeDevice` / `fetchHomeDevices` / `unbindHomeDevice` |

## 验收

- [ ] 有效 6 位码可添加并出现在列表（「已配对」）
- [ ] Hub Topic 前缀为返回的 MAC 紧凑 `device_id`
- [ ] 404/410/409 有可读中文提示
- [ ] 解绑后列表消失；设备可再次配对
- [ ] 默认 UI 不以手输 MAC 为主路径（高级调试折叠）

## 非目标

- 扫码 / BLE 发现
- 浏览器 POST 遥测代替 pairing worker
- Hub 上 pairing 控制面板
