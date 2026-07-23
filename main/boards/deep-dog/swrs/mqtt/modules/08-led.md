# 08 · led（WS2812）

| 项 | 内容 |
|----|------|
| module_id | `led` |
| capabilities | `led` |
| 路由建议 | `/device/:deviceId/modules/led` |
| 契约 | ready（字段）；实现 planned |
| YAML | `led/cmd`、`led/status` |
| 参考 | thumble/diary LedStripControl |

## 入口卡文案

- 标题：灯带  
- 说明：WS2812 模式与颜色  

## 详情页目标

控制 mode / 颜色 / 亮度 / 动画参数；回读 status。

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `led/status` | ↑ | 0 | true |
| `led/cmd` | ↓ | 1 | false |

## 样例 JSON

```json
{
  "mode": 1, "brightness": 128, "r": 0, "g": 255, "b": 0,
  "low_brightness": 16, "interval_ms": 500, "scroll_length": 3,
  "ts": 1710000000
}
```

`mode`：0 关 / 1 静态 / 2 闪烁 / 3 呼吸 / 4 滚动 / 5 系统状态。稀疏 cmd。以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## Steps（前端）

- **Step 1** 校验 `capabilities.led`。
- **Step 2** 订阅 `led/status`。
- **Step 3** UI：模式选择、RGB、亮度；发稀疏 `led/cmd`。
- **Step 4** unmount 退订。

## 固件实现

- planned；移植 thumble/diary 灯带驱动后对接。

## 验收

- [ ] 文档可供前端先做 UI 桩
- [ ] 无 capability 隐藏入口卡
