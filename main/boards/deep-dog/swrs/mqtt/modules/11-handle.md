# 11 · handle（手柄）

| 项 | 内容 |
|----|------|
| module_id | `handle` |
| capabilities | `handle` |
| 路由建议 | `/device/:deviceId/modules/handle` |
| 契约 | 骨架 ready；硬件后补丁 |
| YAML | `handle/cmd`、`handle/status` |

## 入口卡文案

- 标题：手柄  
- 说明：摇杆与按键请求值  

## 详情页目标

展示 `axes` / `buttons`（实际上报控制请求）；可选 enable/disable/pair。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `handle/status` | ↑ | 0 | false |
| `handle/cmd` | ↓ | 1 | false |

## 样例 JSON

```json
{
  "connected": true,
  "source": "bt",
  "axes": { "lx": 0.0, "ly": 0.0, "rx": 0.0, "ry": 0.0 },
  "buttons": {
    "a": false, "b": false, "x": false, "y": false,
    "l1": false, "r1": false, "l2": 0.0, "r2": 0.0,
    "start": false, "select": false
  },
  "raw": {},
  "ts": 1710000000
}
```

```json
{ "action": "enable", "ts": 1710000000 }
```

axes 约 `[-1,1]`。以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## Steps（前端）

- **Step 1** 校验 `capabilities.handle`。
- **Step 2** 订阅 `handle/status`。
- **Step 3** 可视化摇杆/按键；可选发 `handle/cmd`。
- **Step 4** unmount 退订。

## 固件实现

- planned；回调外注，不在协议层绑死狗/臂。

## 验收

- [ ] 前端可按骨架做页
- [ ] 无 capability 隐藏入口卡
