# deep-dog 触摸按键

三键电容触摸（GPIO 1/2/3）驱动与按键应用分层：驱动只上报手势，应用经 Hub + Dispatcher 调度，MQTT 只反映物理态。

## 支持的事件

| 事件 | 说明 |
|------|------|
| `press` | 按下确认 |
| `release` | 抬起确认 |
| `long_press` | 按住约 `DEEP_DOG_TOUCH_LONG_PRESS_MS`（默认 1s）触发一次 |
| `short_press` | 抬起后双击窗超时补发（未触发长按） |
| `double_click` | 同一键双击窗内第二次抬起 |
| `pressed_mask` | bit0/1/2 = 键 1/2/3 当前按下（物理组合） |

时序与队列深度见 [`touch_config.h`](./touch_config.h)。

## 目录

```text
touch_btn/
  touch_button_controller.*   # 驱动采样 + 手势
  touch_event_hub.*           # 事件队列 + 快照
  touch_app_dispatcher.*      # 板级 timer Poll → combo + fan-out
  touch_combo_recognizer.*    # 跨键组合（宏开关）
  touch_app.h                 # ITouchApp 接口
  touch_config.h
  apps/
    touch_app_log.*           # 联调：打印全部事件
    touch_app_dog.*           # 四足运控映射（含时间窗/和弦，待抽离）
    touch_app_servo.*         # 舵机调试占位
  COMBO_EVAL.md               # stub → 06-touch 组合节
  README.md
```

```text
驱动 touch_btn_task
  → TouchEventHub::Push
       ├─（可选）touch_mqtt → touch/status retain
       └─ 队列
            → TouchAppDispatcher::Poll
                 ├─ TouchComboRecognizer（DEEP_DOG_TOUCH_COMBO_ENABLE）
                 └─ log / dog / servo …
```

## 手势栈（自研 vs 官方）

| 层级 | 实现 | 说明 |
|------|------|------|
| 硬件采样 | ESP-IDF `driver/touch_pad` | GPIO→touch channel 映射 |
| 去抖 / 长短按 / 双击 / mask | **自研** `TouchButtonController` | 50ms 轮询；参数见 `touch_config.h` |
| 应用调度 | `TouchEventHub` + `TouchAppDispatcher` | 与 MQTT 解耦 |

仓库内相关官方组件（**本板未接入**）：

| 组件 | 适用 | 与 deep-dog |
|------|------|-------------|
| `espressif/button`（`iot_button`） | GPIO / ADC；短按/长按/双击成熟 | [`common/button`](../../common/button.h) 已用；**不直接覆盖电容 touch_pad 三键** |
| `espressif/touch_button_sensor` | 电容触摸通道 | 其他板已用；可评估替换**采样/阈值**，手势回调与 Hub 仍需板级对齐 `TouchButtonEvent` |

**v0.1 维持自研。** 若抖/误触突出，优先评估 `touch_button_sensor` 替换采样层，对外事件枚举尽量不变。

## 应用与按键映射

应用启用由编译宏决定（见 `touch_config.h`），在 [`esp_sparkbot_board.cc`](../esp_sparkbot_board.cc) `#if` 注册。Dispatcher **fan-out**：每个事件送给全部已注册应用，应用内部按 `button_id` + `event` 过滤。

**不是**「按键 → 单一应用」的运行时绑定；应用映射编译期决定。灵敏度阈值另有 NVS（`touch_thr`）。

### log（`DEEP_DOG_TOUCH_APP_LOG_ENABLE`，默认开）

任意事件 → `ESP_LOGI`（btn / event / mask / raw）。

### dog（`DEEP_DOG_TOUCH_APP_DOG_ENABLE`，默认开；运控需 `DEEP_DOG_DOG_ENABLE`）

| 手势 | 行为 |
|------|------|
| 键1 长按 | `dog.init()` + 3s 组合窗 |
| 键1 short_press | 可选拍照解释（`DEEP_DOG_TOUCH2_SHORT_PHOTO_EXPLAIN`） |
| 键2/3 press | 小步前进/后退（非组合窗；press 触发以免双击窗延迟） |
| 键2/3 长按 | 持续前进/后退；组合窗内 → 站立/趴下 |
| 组合窗内 short_press 2/3 | 大步前进/后退 |
| 键2+3 短按释放 | 停止持续行走（应用内和弦；通用组合层见评估） |
| double_click | 未使用 |

`DEEP_DOG_DOG_ENABLE=0` 时仅保留键1 short（拍照解释等），长按1 打日志忽略。

通用组合键见 [06-touch · 组合键](../swrs/mqtt/modules/06-touch.md#组合键识别层)。狗控仍用本表；`DEEP_DOG_TOUCH_COMBO_ENABLE`（默认 1）开启识别与 MQTT `last_combo`，`COMBO_CONSUME` 默认 0 不吞单键。

### servo（`DEEP_DOG_TOUCH_APP_SERVO_ENABLE`，默认跟随 `DEEP_DOG_SERVO_ENABLE`）

| 手势 | 行为（占位） |
|------|----------------|
| short_press 1/2/3 | 日志：预留微调 |
| 长按 1 | 日志：预留归中 |
| double_click | 日志：预留 |

## 检测灵敏度阈值（A 类）

判定：`press_abs = max(baseline * ratio + offset, press_abs_min[i])`。

| 参数 | 出厂默认 | 说明 |
|------|----------|------|
| `press_abs_ratio` | 0.05 | 相对 baseline |
| `release_abs_ratio` | 0.03 | 抬起 |
| `press_abs_offset` / `release_abs_offset` | 200 / 120 | 比例附加 |
| `press_abs_min[1/2/3]` | **2600 / 800 / 500** | 每键按下下限（键2/3 已下调） |
| `release_abs_min[1/2/3]` | **2200 / 650 / 400** | 每键抬起下限 |
| `debounce_cycles` | 2 | 去抖 |
| `baseline_alpha` 等 | 见 `touch_config.h` | 空闲 baseline 慢跟 |

运行时：`TouchButtonController::Get/SetThresholds`；NVS namespace `touch_thr`。  
MQTT：`touch/status.thresholds` 始终上报；`touch/cmd` 调参/标定。

### 标定（单键软检测）

1. `touch/cmd`：`{ "action":"calibrate", "button_id":3, "samples":10 }`
2. 阶段 `idle`（~500ms 采噪声）→ `collect`（软阈采峰）→ `done`（`press_min≈min(peaks)*0.45`，写 NVS）
3. 进度：`touch/status.calib`；标定中强制带 `debug[]`

亦支持 `set_thresholds` / `reset_thresholds` / `cancel_calibrate`。

## 为何 MQTT 不含应用语义

- `touch/status` 是物理态 + 阈值；同一手势可 fan-out 多应用。
- Web「键做什么」：用本文映射表静态说明，**不是**把 action 塞进 uplink。
- **动作绑定**（短按→某业务）仍未立项；**阈值/标定已支持**（与绑定不同）。

## 组合层

- 实现：[`touch_combo_recognizer`](./touch_combo_recognizer.h)；契约：[06-touch.md](../swrs/mqtt/modules/06-touch.md#组合键识别层)
- `DEEP_DOG_TOUCH_COMBO_ENABLE`（默认 1）：打开识别 + MQTT `last_combo`
- `DEEP_DOG_TOUCH_COMBO_CONSUME`（默认 0）：命中后是否跳过应用 fan-out

## 新增应用

1. 实现 `ITouchApp`（`OnEvent` / `Name`），放在 `apps/`。
2. 在 `touch_config.h` 增加 `DEEP_DOG_TOUCH_APP_xxx_ENABLE`。
3. 在 [`esp_sparkbot_board.cc`](../esp_sparkbot_board.cc) `#if` 注册到 `touch_dispatcher_`。
4. **不要改** `TouchButtonController`（调灵敏用阈值/标定 API）。
5. 同步更新本文「应用与按键映射」与 [06-touch.md](../swrs/mqtt/modules/06-touch.md) 对照摘要。

## MQTT

| Topic | 方向 | 说明 |
|-------|------|------|
| `touch/status` | ↑ retain | 物理快照 + `thresholds` + `calib` |
| `touch/cmd` | ↓ | `set_thresholds` / `calibrate` / `reset_thresholds` / `cancel_calibrate` |

契约：[`swrs/mqtt/modules/06-touch.md`](../swrs/mqtt/modules/06-touch.md)

## 联调建议

1. 开 LOG，确认短按/双击；键3 不灵时先看 `thresholds.buttons[2].press_abs_min` 或发 `calibrate`。
2. 前端订 status，滑条改阈或点标定；重启验证 NVS。
3. 再开 DOG / SERVO。
