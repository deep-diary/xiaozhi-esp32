# 触摸按键（Deep Dog）

本目录包含 **底层采样** 与 **整机业务逻辑** 两层，便于按键规则变复杂时仍保持板级文件精简。

## 文件

| 文件 | 作用 |
|------|------|
| `touch_button_controller.h` / `.cc` | 触摸通道滤波、按下/抬起/长按检测，通过回调上报 `TouchButtonEvent`（**无机器狗语义**）。 |
| `deep_dog_touch_app.h` / `.cc` | **DeepDog 专用**：把触摸事件映射为 `DogControl` 动作（初始化、站立、趴下、前进一步等）。 |

板级入口（如 `esp_sparkbot_board.cc`）只负责创建 `TouchButtonController`、`DogControl`，并将回调转给 `DeepDogTouchApp::OnTouchEvent`。

---

## 按键编号

与硬件/配置一致：**1 / 2 / 3** 对应三只触摸键（见 `config.h` 中 `TOUCH_BUTTONn_GPIO`）。

---

## 当前映射（调试向）

| 操作 | 动作 |
|------|------|
| **长按 1** | 整机 `init()`（趴姿下写零、使能等）；成功后打开 **组合键窗口**（默认 3 秒） |
| **长按 1 → 短按 2**（见下） | `stand()` 站立 |
| **长按 1 → 短按 3**（见下） | `lieDown()` 趴下 |
| **短按 2**（无组合窗口时） | `goForward()` 前进一步 |
| **短按 3**（无组合窗口时） | `goBack()` 后退一步 |
| **长按 2** | `goForwardSteps(5)` 连续前进 5 步 |
| **长按 3** | `goBackSteps(5)` 连续后退 5 步 |

### 「长按 1 → 短按 2 / 3」如何区分短按与长按

- **短按**：按下后在 **未触发该键长按** 前释放 → 在 **释放** 时判定为短按（避免与「长按 2/3 连发 5 步」冲突）。
- **长按 2 / 3**：仍由 `TouchButtonController` 在按住足够周期后上报 `kLongPress`，业务层据此执行 5 步。

组合键窗口在 **长按 1** 成功后启动；完成一次「站立」或「趴下」后关闭，超时也会关闭。

---

## 扩展建议

- 新增组合键、多键手势：在 `DeepDogTouchApp` 内增加状态变量与超时，**勿**在 `touch_button_controller` 里写业务。
- 与 `DogPoseState` / 灯带联动：在板级根据 `dog.getPoseState()` 更新显示，触摸层只负责调用 `DogControl`。

---

## 日志 TAG

- 底层扫描任务：`deep_dog_touch`（见 `touch_button_controller.cc`）
- 业务映射：`dog_touch`（见 `deep_dog_touch_app.cc`）
