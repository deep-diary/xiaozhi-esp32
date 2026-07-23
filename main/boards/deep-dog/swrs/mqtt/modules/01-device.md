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
- 说明：固件、IP、能力列表  

## 详情页目标

展示完整 `device/info` 与心跳 `device/status`；无下行 cmd（v0.1）。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `device/info` | ↑ | 0 | true |
| `device/status` | ↑ | 0 | false |

## 样例 JSON

```json
{
  "device_id": "deep-dog-dev",
  "firmware": "0.0.0",
  "ip": "192.168.31.211",
  "http_port": 8080,
  "capabilities": {
    "dog": true, "stream": true, "face": true, "imu": true,
    "led": false, "servo": false, "gimbal": false,
    "handle": false, "touch": true, "can": true
  },
  "ts": 1710000000
}
```

字段以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## Steps（前端）

- **Step 1** 进入页（或设备页页头复用同一数据源）。
- **Step 2** 确保已订阅 `device/info`（可与设备页共享连接状态）。
- **Step 3** 渲染字段表 + capabilities 开关一览（只读）。
- **Step 4** 可选订阅 `device/status` 显示 online/uptime。
- **Step 5** 离页时若本页独占订阅则退订。

## 固件实现

- 上线/重连后 publish `device/info`（retain）。
- 周期 publish `device/status`（建议 ~0.2 Hz）。
- `capabilities.*` 与编译宏 `DEEP_DOG_*_ENABLE` 对齐。

## 验收

- [ ] 前端能展示 capabilities 并驱动入口卡显隐
- [ ] retain 晚订阅仍能拿到 info
