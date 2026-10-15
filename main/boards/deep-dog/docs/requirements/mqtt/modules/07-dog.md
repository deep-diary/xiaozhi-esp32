# 07 · dog（四足）

| 项 | 内容 |
|----|------|
| module_id | `dog` |
| capabilities | `dog`（可裁剪） |
| 路由建议 | `/device/:deviceId/modules/dog` |
| 契约 | ready（字段） |
| YAML | `dog/cmd`、`dog/status` |

## 入口卡文案

- 标题：机器狗  
- 说明：动作控制与初始化状态  

## 详情页目标

显示 `dog_initialized` / `has_fault`；按钮下发动作 cmd。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `dog/status` | ↑ | 0 | true |
| `dog/cmd` | ↓ | 1 | false |

## 样例 JSON

```json
{ "cmd": "forward", "ts": 1710000000 }
```

```json
{ "dog_initialized": true, "has_fault": false, "ts": 1710000000 }
```

`cmd`：`init` / `forward` / `back` / `stand` / `liedown` / `dance` / `stop_walk` / `disable`。以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## Steps（前端）

- **Step 1** 校验 `capabilities.dog`（非狗产品应无此卡）。
- **Step 2** 订阅 `dog/status`。
- **Step 3** 未 init 时仅开放 init；已 init 开放动作按钮。
- **Step 4** 发布 `dog/cmd`。
- **Step 5** unmount 退订。

## 固件实现

- 映射 `POST /api/cmd`、`dog_initialized` / `/api/dog_status` 摘要。
- HTTP 已具备；MQTT 后接。

## 验收

- [ ] 详情页可发 cmd 且 status 反馈
- [ ] `dog=false` 时无入口卡
