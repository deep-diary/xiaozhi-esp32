# 06 · touch（触摸键）

| 项 | 内容 |
|----|------|
| module_id | `touch` |
| capabilities | `touch` |
| 路由建议 | `/device/:deviceId/modules/touch` |
| 契约 | ready；驱动 + `touch_mqtt` + 阈值/标定 |
| YAML | `touch/status`、`touch/cmd` |
| 参考 | [touch_btn](../../../touch_btn/) |

## 入口卡文案

- 标题：触摸键  
- 说明：三键按下 / 长按 / 短按 / 双击  

## 详情页目标

可视化三键 `pressed` / `long_press` / `last_event`、`pressed_mask`；展示并可调 `thresholds`；支持单键标定。

展示建议：

- 三键高亮 `pressed`；长按角标；`last_event` 文案
- 底部 `pressed_mask`
- 阈值滑条（每键 `press_abs_min` / `release_abs_min`；高级区 ratio）
- 「标定键 N」按钮 + `calib` 进度；「恢复出厂」
- 标定中显示 `debug[].abs_diff`（固件标定中强制带 debug）

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `touch/status` | ↑ | 0 | true |
| `touch/cmd` | ↓ | 1 | false |

触发：按键事件 / 改阈 / 标定进度（约 250ms）/ 重连。retain 便于晚进页。

## 字段表（status）

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | bool | 是否有数据源 |
| `pressed_mask` | int | bit0/1/2 = 键1/2/3 |
| `buttons` | array[3] | 三键快照 |
| `buttons[].last_event` | enum | `press` \| `release` \| `long_press` \| `short_press` \| `double_click` |
| `thresholds` | object | **始终**上报；见下 |
| `calib` | object | 标定进度 / 最近结果 |
| `last_combo` | object | 可选；组合命中 |
| `debug` | array | 标定中强制；或 `DEEP_DOG_TOUCH_MQTT_DEBUG` / cmd `debug:true` |
| `ts` | int | Unix 秒 |

### thresholds

| 字段 | 说明 |
|------|------|
| `press_abs_ratio` / `release_abs_ratio` | 相对 baseline |
| `press_abs_offset` / `release_abs_offset` | 比例项附加 |
| `debounce_cycles` | 去抖周期 |
| `buttons[].press_abs_min` / `release_abs_min` | 每键绝对下限（出厂默认键3=`500`/`400`） |

### calib

| 字段 | 说明 |
|------|------|
| `active` | 是否进行中 |
| `button_id` | 1–3 |
| `phase` | `idle` \| `collect` \| `done` \| `fail` |
| `count` / `samples` | 已采峰 / 目标次数 |
| `error` | 失败时：`timeout` / `cancelled` 等 |

## touch/cmd

| action | 说明 |
|--------|------|
| `set_thresholds` | 改全局 ratio/offset 与/或 `buttons[{id,press_abs_min,release_abs_min}]`；`persist` 默认 true |
| `calibrate` | `button_id`（默认 3）、`samples`（默认 10）；软检测采峰，不依赖当前过高 min |
| `reset_thresholds` | 恢复出厂并 persist |
| `cancel_calibrate` | 取消 |

可选顶层 `debug: true` 强制 status 带 `debug[]`。

样例：

```json
{ "action": "set_thresholds", "buttons": [{ "id": 3, "press_abs_min": 400, "release_abs_min": 300 }], "persist": true }
```

```json
{ "action": "calibrate", "button_id": 3, "samples": 10 }
```

## 样例 JSON（status）

```json
{
  "ok": true,
  "pressed_mask": 0,
  "buttons": [
    { "id": 1, "pressed": false, "long_press": false, "last_event": "release" },
    { "id": 2, "pressed": false, "long_press": false, "last_event": "release" },
    { "id": 3, "pressed": false, "long_press": false, "last_event": "short_press" }
  ],
  "thresholds": {
    "press_abs_ratio": 0.05,
    "release_abs_ratio": 0.03,
    "press_abs_offset": 200,
    "release_abs_offset": 120,
    "debounce_cycles": 2,
    "buttons": [
      { "id": 1, "press_abs_min": 2600, "release_abs_min": 2200 },
      { "id": 2, "press_abs_min": 800, "release_abs_min": 650 },
      { "id": 3, "press_abs_min": 500, "release_abs_min": 400 }
    ]
  },
  "calib": { "active": false, "button_id": 3, "phase": "done", "count": 10, "samples": 10 },
  "ts": 1710000000
}
```

以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

## 按键语义对照（非 MQTT 字段）

`touch/status` **物理态 + 阈值**；不含 dog/servo 业务动作名。Web 静态说明见 [touch_btn/README.md](../../../touch_btn/README.md#应用与按键映射)。

| 手势 | log | dog（`DEEP_DOG_DOG_ENABLE`） | servo（占位） |
|------|-----|------------------------------|---------------|
| 任意事件 | 串口打印 | — | — |
| 键1 长按 | — | `dog.init()` + 3s 组合窗 | 预留归中 |
| 键1 short_press | — | 可选拍照解释 | 预留微调 |
| 键2/3 press | — | 小步前/后 | — |
| 键2/3 长按 | — | 持续走；组合窗内站立/趴下 | 日志 |
| 键2/3 short（组合窗） | — | 大步前/后 | 预留微调 |
| 键2+3 短按释放 | — | 停持续走 | — |

## 组合键（识别层）

见既有白名单；`last_combo` 与阈值无关。宏：`DEEP_DOG_TOUCH_COMBO_ENABLE` / `COMBO_CONSUME`。

| id | 类型 | 设备语义（deep-dog） |
|----|------|----------------------|
| `chord_short_1_2` / `_1_3` / `_2_3` | Chord | （业务层，见狗控表） |
| `hold1_tap2` | Hold+Tap | 进入配对 / 播报码 |
| `hold1_tap3` | Hold+Tap | 请求解绑（`pairing/request`） |

## Steps（前端）

1. 校验 `capabilities.touch`。
2. 订 `touch/status`；渲染三键 + `thresholds` 滑条。
3. 调阈 → `touch/cmd` `set_thresholds`；标定 → `calibrate`，看 `calib.count`。
4. unmount 退订 status（不必长期订 cmd）。

## 固件实现

- `TouchThresholds` 运行时结构；NVS `touch_thr`；开机 `LoadThresholdsFromNvs`
- 标定：idle 采噪声 → soft_thr 采 N 峰 → `press_abs_min = min(peaks)*0.45`（夹紧）并 persist
- `touch_mqtt` 订 `touch/cmd`；标定中约 250ms 刷新 status

## 验收

- [x] 按下详情页即时更新
- [x] `thresholds` 始终在 status
- [x] `set_thresholds` / `calibrate` / `reset_thresholds` 契约 + 固件
- [ ] 实机键3：默认或标定后短按稳定
- [ ] 标定 10 次后 NVS 重启仍在
