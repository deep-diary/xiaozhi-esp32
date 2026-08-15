# Deep-Dog 机器狗板级说明

> **配置入口**：硬件脚与引出脚模式见 [`config.h`](./config.h)；功能组合见 [`board_features.h`](./board_features.h)；剖面表见 [`FEATURE_FLAGS.md`](./FEATURE_FLAGS.md)。

## 功能需求

通过**语音**控制机器狗完成：
- **前进 / 后退**
- **跳舞**
- 以及后续扩展：站立、卧倒、左转、右转等

语音由主项目（小智 / 语音助手）识别后，通过 MCP 工具调用本板级实现的底盘控制接口，实现只在本目录内完成机器狗逻辑，不影响主项目其他板型。

---

## 板级目录结构

```
main/boards/deep-dog/
├── README.md / FEATURE_FLAGS.md
├── config.h                  # 板级硬件 IO + EXT_PIN 成对模式
├── board_features.h          # 分层 ENABLE / 非引脚总开关
├── config.json
├── esp_sparkbot_board.cc     # 板级编排
├── can/ motor/ leg/ dog/ trajectory/
├── uart/ led/ servo/ gimbal/ arm/ rs485/ io_ext/ ad/   # 引出脚相关占位
├── mqtt/ vision/ face_ai/ http-server/ …
└── swrs/                     # 需求与 MQTT 契约
```

- **编译**：主项目 `CMakeLists.txt` 根据 `CONFIG_BOARD_TYPE_DEEP_DOG` 选择 `BOARD_TYPE=deep-dog`，并通过 `GLOB_RECURSE` 递归收集本目录及子目录下所有 `*.cc`/`*.cpp`/`*.c` 参与编译，无需修改主项目源列表。
- **头文件**：主项目已把 `boards/deep-dog` 加入 include 路径，本目录内引用使用相对路径即可，例如 `#include "dog/dog_control.h"`、`#include "can/ESP32-TWAI-CAN.hpp"`。
- **同步板级配置**：修改 [`config.json`](config.json) 摄像头等 Kconfig 后，须同步 `sdkconfig`（例如 `python3 scripts/release.py deep-dog` 删除旧 zip 后重跑，或按 `config.json` 的 `sdkconfig_append` 手工改 `sdkconfig`），再 `idf.py build` / 烧录。

---

## 软件架构（分层）

```
┌─────────────────────────────────────────────────────────────┐
│  语音 / MCP 工具（主项目 Application + McpServer）            │
│  self.chassis.go_forward / go_back / dance / stand / lie…   │
└───────────────────────────┬─────────────────────────────────┘
                            │ 仅在本板 esp_sparkbot_board.cc 中注册
                            ▼
┌─────────────────────────────────────────────────────────────┐
│  dog/  整机控制（DogControl）                                 │
│  前进/后退/跳舞/站立/卧倒；步态编排；4 条腿协同                │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────┼─────────────────────────────────┐
│  leg/  单腿控制（LegControl）                                 │
│  单腿步态相位、站立/卧倒/迈一步；3 关节目标位置               │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────┼─────────────────────────────────┐
│  trajectory/  轨迹规划（可选）                                │
│  点对点、插值，用于平滑关节运动或跳舞序列                     │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────┼─────────────────────────────────┐
│  motor/  电机控制（DeepMotor + 协议）                         │
│  12 个电机：使能、位置/速度、反馈                              │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────┼─────────────────────────────────┐
│  can/  CAN 总线（TWAI）                                      │
│  单路 CAN，连接所有电机                                       │
└─────────────────────────────────────────────────────────────┘
```

---

## 与主项目的关系

- **板型选择**：`Kconfig` 中 `CONFIG_BOARD_TYPE_DEEP_DOG` 对应板型 `deep-dog`，仅在本板编译时生效。
- **入口**：`esp_sparkbot_board.cc` 中的 `DeepDog` 类继承自 `WifiBoard`，通过 `DECLARE_BOARD(DeepDog)` 注册；初始化时注册 MCP 工具（如 `self.chassis.go_forward`、`go_back`、`dance` 等）。
- **语音链路**：主项目语音识别 → 大模型/对话 → 调用 MCP 工具 → 仅 deep-dog 板注册的工具会执行 → 调用本目录内的 `DogControl`（或等价接口），实现前进/后退/跳舞等。
- **开发原则**：**尽量只在本目录（`main/boards/deep-dog/`）内新增/修改代码**；必须动主项目时，仅做最小改动（如 Kconfig 已存在的选项、已有板型分支），不改变其他板型的逻辑。

---

## 硬件与配置要点

- **CAN**：`EXT_PIN_MODE=CAN` 时由 `can/can_config.h` 将 A/B 映射为 `CAN_TX_GPIO` / `CAN_RX_GPIO`；开关见 `board_features.h`。
- **电机**：12 个无刷电机，一路 CAN，ID 分配与腿/关节对应关系见 `dog/README.md`。
- **UART 底盘**：原 SparkBot 底盘为 UART 控制；机器狗改为 CAN 控制电机，因此不再使用 UART 底盘指令（`SendUartMessage`），由 `dog` + `motor` + `can` 替代。

---

## 开发计划概要

| 阶段 | 内容 | 说明 |
|------|------|------|
| 1 | 单电机 MCP + CAN | 在 motor 层验证 CAN 通信与单电机控制，必要时 MCP 工具调试 |
| 2 | 单腿控制 | 实现 leg：站立/卧倒/迈一步（正弦步态），依赖 motor |
| 3 | 整机步态 | 实现 dog：4 腿协同、前进/后退、站立/卧倒 |
| 4 | 语音对接 | 将 MCP 工具从 UART 改为调用 dog_control，实现语音前进/后退 |
| 5 | 跳舞与扩展 | 编排跳舞动作（可复用 trajectory），并扩展左转/右转等 |

详细任务与依赖见各子目录 README 及下文「开发计划」章节。

---

## 开发计划（分步实施）

1. **阶段 1：单电机与 CAN**
   - 确保 CAN 初始化、电机使能/位置控制通过 MCP 或本地测试可调。
   - 见 `motor/README.md`、`can/README.md`。

2. **阶段 2：单腿**
   - 实现 `LegControl`：关节限位、站立位、卧倒、基于正弦的迈一步（前进/后退）。
   - 见 `leg/README.md`。

3. **阶段 3：整机步态**
   - 实现 `DogControl`：4 腿、步态相位、前进/后退循环。
   - 不依赖 URDF，直接关节角规划。
   - 见 `dog/README.md`。

4. **阶段 4：语音与 MCP**
   - 在 `esp_sparkbot_board.cc` 中，将 `self.chassis.*` 从 `SendUartMessage` 改为调用 `DogControl` 的对应接口（前进/后退/站立/卧倒/跳舞）。
   - 确保语音指令能触发对应动作。

5. **阶段 5：跳舞与优化**
   - 设计简单跳舞序列（多关键帧 + trajectory 插值或固定时序）。
   - 可选：左转/右转、速度/步幅参数暴露为 MCP 参数。

各子模块的接口约定、数据结构与注意事项见对应子目录 README。

更细的**分阶段任务表**与完成标准见 [swrs/dog/DEVELOPMENT_PLAN.md](./swrs/dog/DEVELOPMENT_PLAN.md)；视觉/人脸/推流见 [swrs/ROADMAP.md](./swrs/ROADMAP.md)。
