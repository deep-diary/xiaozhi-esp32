# 触摸按键（Deep Dog）

本目录包含 **底层采样** 与 **整机业务逻辑** 两层，便于按键规则变复杂时仍保持板级文件精简。

## 文件

| 文件 | 作用 |
|------|------|
| `touch_button_controller.h` / `.cc` | 触摸通道滤波、按下/抬起/长按检测，通过回调上报 `TouchButtonEvent`（**无机器狗语义**）。 |
| `deep_dog_touch_app.h` / `.cc` | **DeepDog 专用**：把触摸事件映射为 `DogControl` 动作。 |

板级入口（如 `esp_sparkbot_board.cc`）只负责创建 `TouchButtonController`、`DogControl`，并将回调转给 `DeepDogTouchApp::OnTouchEvent`。

---

## 按键编号

与硬件/配置一致：**1 / 2 / 3** 对应三只触摸键（见 `config.h` 中 `TOUCH_BUTTONn_GPIO`）。

---

## 当前映射

| 操作 | 动作 |
|------|------|
| **长按 1** | 整机 `init()`；成功后打开 **组合键窗口**（默认 3 秒） |
| **无组合窗口：短按 2 / 3** | `goForward()` / `goBack()` **一小步** |
| **无组合窗口：长按 2 / 3** | `startContinuousForward()` / `startContinuousBackward()` **持续走**；**短按 2+3**（曾同时按下且均未长按）→ `stopContinuousLocomotion()` |
| **长按 1 后的组合窗口内：短按 2 / 短按 3**（释放时判定为短按） | `goForwardBigStep()` / `goBackBigStep()` **一大步** |
| **长按 1 后的组合窗口内：长按 2 / 长按 3** | `stand()` / `lieDown()`，并关闭组合窗口 |

### 短按 vs 长按（2 / 3）

- **短按**：按下后在 **未触发该键长按** 前释放 → 在 **释放** 时执行组合窗内的一大步；无窗时在 **按下** 时即迈一小步。
- **长按**：`TouchButtonController` 上报 `kLongPress` 时：无窗→持续走；有窗→站立/趴下。

---

## 设计说明

- **1** 负责 **init + 开窗**；**2/3** 在无窗时兼顾 **点动（小步）** 与 **持续走**；开窗后 **短=大步、长=姿态**，与「先初始化再调姿态/试走」的流程一致。
- **一大步**在 **释放** 时执行（组合窗内），避免与长按判定冲突；**一小步**在无窗时 **按下** 即执行，反馈快。
- init 后各电机反馈位置见串口 `DogControl` 的 `[init 后反馈]` 日志（`dog_control.cc`）。

---

## 扩展建议

- 新增组合键：在 `DeepDogTouchApp` 内增加状态与超时，**勿**在 `touch_button_controller` 里写业务。
- 灯带：板级根据 `dog.getPoseState()` 更新显示。

---

## 日志 TAG

- 底层：`deep_dog_touch`
- 业务：`dog_touch`
