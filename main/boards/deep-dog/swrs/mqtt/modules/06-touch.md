# 06 · touch（触摸键）

| 项 | 内容 |
|----|------|
| module_id | `touch` |
| capabilities | `touch` |
| 路由建议 | `/device/:deviceId/modules/touch` |
| 契约 | ready；驱动 + `touch_mqtt` |
| YAML | `touch/status` |
| 参考 | [touch_btn](../../../touch_btn/) |

## 入口卡文案

- 标题：触摸键  
- 说明：三键按下 / 长按 / 短按 / 双击  

## 详情页目标

只读可视化三键 `pressed` / `long_press` / `last_event`，以及 `pressed_mask`（物理同时按下位图）。v0.1 无 cmd。

展示建议：

- 三键高亮 `pressed`
- 长按角标 `long_press`
- `last_event` 文案（含 `press` / `release` / `long_press` / `short_press` / `double_click`）
- 底部显示 `pressed_mask`（bit0/1/2 = 键1/2/3）
- 可选：按 [按键语义对照](#按键语义对照非-mqtt-字段) 展示静态说明（tooltip），**勿**期望报文内带应用语义

## Topic

| Topic | 方向 | QoS | retain |
|-------|------|-----|--------|
| `touch/status` | ↑ | 0 | true |

触发：Hub 每次按键事件 Push 时发**三键完整快照**（非单事件 JSON）；MQTT 重连成功后再发一次。retain 便于晚进页拿到当前态。

## 字段表

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | bool | 是否有 Hub/Controller 数据源 |
| `pressed_mask` | int | bit0/1/2 = 键1/2/3 当前物理按下 |
| `buttons` | array[3] | 三键快照 |
| `buttons[].id` | int | `1` / `2` / `3` |
| `buttons[].pressed` | bool | 是否按下 |
| `buttons[].long_press` | bool | 是否处于长按态 |
| `buttons[].last_event` | enum | 见下 |
| `ts` | int | Unix 秒 |
| `last_combo` | object | 可选；组合命中后出现，见下 |
| `debug` | array | 可选；固件 `DEEP_DOG_TOUCH_MQTT_DEBUG=1` 时含 raw/baseline |

`last_event` ∈ `press` | `release` | `long_press` | `short_press` | `double_click`。  
以 [YAML](../protocol/deep-dog-mqtt.yml) 为准。

`last_combo`（可选，**不**写入 `buttons[].last_event`）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | enum | `chord_short_1_2` \| `chord_short_1_3` \| `chord_short_2_3` \| `hold1_tap2` \| `hold1_tap3` |
| `ts` | int | 组合识别 Unix 秒 |

## 样例 JSON

同一时刻的 retain 快照；各键 `last_event` **不必**同时发生。含组合时示例：

```json
{
  "ok": true,
  "pressed_mask": 0,
  "buttons": [
    { "id": 1, "pressed": false, "long_press": false, "last_event": "release" },
    { "id": 2, "pressed": false, "long_press": false, "last_event": "release" },
    { "id": 3, "pressed": false, "long_press": false, "last_event": "double_click" }
  ],
  "last_combo": { "id": "chord_short_2_3", "ts": 1710000000 },
  "ts": 1710000000
}
```

无组合命中前可无 `last_combo` 字段。其它合法 `last_event`：`"press"` / `"short_press"` / `"long_press"`。可选 `debug[]`。

## 按键语义对照（非 MQTT 字段）

`touch/status` **只反映物理态**，不含 dog/servo/log 业务含义。Web 若需说明「键1 短按做什么」，请用编译期映射表做**静态文案**（tooltip / 帮助），勿往 uplink 塞 `action` / `app` 字段。

完整表见 [touch_btn/README.md · 应用与按键映射](../../../touch_btn/README.md#应用与按键映射)。摘要：

| 手势 | log | dog（`DEEP_DOG_DOG_ENABLE`） | servo（占位） |
|------|-----|------------------------------|---------------|
| 任意事件 | 串口打印 | — | — |
| 键1 长按 | — | `dog.init()` + 3s 组合窗 | 预留归中 |
| 键1 short_press | — | 可选拍照解释 | 预留微调 |
| 键2/3 press | — | 小步前/后 | — |
| 键2/3 长按 | — | 持续走；组合窗内站立/趴下 | 日志 |
| 键2/3 short（组合窗） | — | 大步前/后 | 预留微调 |
| 键2+3 短按释放 | — | 停持续走（和弦） | — |
| double_click | — | （dog 未用，空槽） | 预留 |

应用启用由 `DEEP_DOG_TOUCH_APP_*_ENABLE` 编译期决定；事件经 `TouchAppDispatcher` **fan-out** 到全部已注册应用。

## 组合键（识别层）

通用跨键组合；**与 dog/servo 业务映射无关**。固件：[`touch_combo_recognizer`](../../../touch_btn/touch_combo_recognizer.h)，经 `TouchAppDispatcher` 在 fan-out 前 `Feed`。

### 硬件几何

| 逻辑键 | 物理分布 |
|--------|----------|
| 键1 | 正方体**两侧**各一片（同一 channel） |
| 键2 / 键3 | 顶面左 / 右半边 |

### 白名单（5 个）

| id | 类型 | 识别摘要 |
|----|------|----------|
| `chord_short_1_2` | Chord | 1+2 重叠后短抬（&lt; 长按阈） |
| `chord_short_1_3` | Chord | 1+3 重叠后短抬 |
| `chord_short_2_3` | Chord | 2+3 重叠后短抬 |
| `hold1_tap2` | Hold+Tap | 键1 仍按下时键2 `press` |
| `hold1_tap3` | Hold+Tap | 键1 仍按下时键3 `press` |

优先级：键1 已按下再出 2/3 `press` → **hold+tap**；两键近乎同时按下再同抬 → **chord**。不做 `chord_long_*`、不做时间窗 Seq。

### 宏

| 宏 | 默认 | 说明 |
|----|------|------|
| `DEEP_DOG_TOUCH_COMBO_ENABLE` | `1` | `0` 关闭识别与 `last_combo` |
| `DEEP_DOG_TOUCH_COMBO_CONSUME` | `0` | `1` 时命中组合跳过应用 fan-out；默认 `0` 以免影响现有 dog |

命中时：串口 `touch_combo` + 补发 `touch/status`（带 `last_combo`）。狗控映射暂不迁到组合层。

## 评估 · Web 可配置绑定（未来 / 未立项）

**技术可行，v0.1 不做。** 若要「Web 指定短按=某动作并写入 NVS」，大致需要：

1. 配置模型：`button_id × gesture → action_id`（或覆盖默认映射）
2. 下行 `touch/cmd` + 上行 `touch/bindings`（retain）确认
3. NVS 持久化；启动加载覆盖编译期默认
4. Dispatcher / 应用从硬编码改为查表
5. 非法 action、与 dog 状态机冲突、出厂回滚

当前：**无** `touch/cmd`、**无** NVS 按键绑定。

## 手势实现说明

| 层级 | 实现 |
|------|------|
| 采样 | ESP-IDF `driver/touch_pad` |
| 去抖 / 长短按 / 双击 / `pressed_mask` | 板级自研 [`TouchButtonController`](../../../touch_btn/touch_button_controller.cc) |
| 官方对照 | 仓库 `common/button`（`iot_button`，GPIO/ADC）不直接覆盖本板电容三键；`espressif/touch_button_sensor` 可评估替换采样层，手势与 Hub 仍需板级 |

**v0.1 维持自研**；误触问题突出时再评估迁移采样层。细节见 [touch_btn/README.md](../../../touch_btn/README.md)。

## Steps（前端）

- **Step 1** 校验 `capabilities.touch`。
- **Step 2** 订阅 `touch/status`（retain，晚进页也有当前态）。
- **Step 3** 三键 UI 高亮 + `last_event` + `pressed_mask`；若有 `last_combo` 可展示最近组合 id。
- **Step 4** unmount 退订。

## 固件实现

- `TouchButtonController` → `TouchEventHub`；Hub Push 时 `touch_mqtt` 发整包三键快照。
- `TouchComboRecognizer`（`DEEP_DOG_TOUCH_COMBO_ENABLE`）：Dispatcher Poll 识别；命中补发 `touch/status` 含 `last_combo`。
- 与 `dog` / `servo` 业务解耦：`buttons[].last_event` 仍为物理单键；组合只在 `last_combo`。
- 按键应用经 `TouchAppDispatcher` 板级 timer 调度，见 [touch_btn/README.md](../../../touch_btn/README.md)。

## 验收

- [x] 按下时详情页即时更新（retain 快照）
- [x] 无 capability 隐藏入口卡
- [x] `press` / `short_press` / `double_click` / `long_press` / `release` / `pressed_mask` 可展示
- [x] 文档对照表可供 Web 静态说明（报文不含应用语义）
- [x] 契约含可选 `last_combo`（YAML + 固件）
- [ ] 实机组合后 MQTT / 串口可见对应 id
