# LL-001 板级编译配置与联调踩坑合集

记录时间：2026-10-14
场景：deep-dog（ESP32-S3）配置 OV3660 摄像头 + 打通 MQTT 联调期间遇到的一组问题。

---

## LL-001-1 手工编辑 `sdkconfig` 导致 OV3660 摄像头初始化失败

- **现象**：
  手工往 `sdkconfig` 追加 OV3660 配置后，相机能读到 PID 但初始化失败：
  ```
  I (2659) ov3660: Detected Camera sensor PID=0x3660
  E (2679) sccb_i2c: s_sccb_i2c_transmit_reg_a16v8(92): failed to i2c transmit
  E (2679) ov3660: ov3660_set_format(518): Set common regs failed
  ```
  重启重试更差（第二次连 PID 都读不到，`address=3c` 探测失败）。
  一度误判为 XCLK 频率 / 硬件 / FPC 问题。

- **根因**：
  **手工编辑的 `sdkconfig` 与 Kconfig 的依赖/choice/顺序不一致**，导致传感器相关配置未被正确闭合（能看到 `CONFIG_CAMERA_OV3660=y`，但整体配置状态不完整）。`sdkconfig` 是生成物，不应手改。

- **正确做法**：
  只改板级源 `main/boards/deep-dog/config.json` 的 `sdkconfig_append`，然后用官方流程**干净重生成** `sdkconfig`：
  ```bash
  rm -f sdkconfig sdkconfig.old && rm -rf build
  idf.py set-target esp32s3
  # 追加 config.json 的 sdkconfig_append + CONFIG_BOARD_TYPE_DEEP_DOG=y
  idf.py -DBOARD_NAME=deep-dog -DBOARD_TYPE=deep-dog build
  ```
  或直接：`python scripts/release.py deep-dog`（内部即按 config.json 重配 + append）。

- **验证**：
  ```bash
  grep CONFIG_CAMERA_OV3660 build/config/sdkconfig.h   # 应为 1
  ```
  启动日志出现 `I ... EspVideo: Camera init success` 即成功。

- **结论**：**改板级编译配置 = 改 `config.json` + 干净重生成，绝不手改 `sdkconfig`。**

- **相关文件**：`main/boards/deep-dog/config.json`、`main/boards/deep-dog/CAMERA_SENSOR.md`、`scripts/release.py`

---

## LL-001-2 `idf.py monitor` 残留进程占用串口，导致 flash 失败

- **现象**：
  `idf.py -p /dev/cu.usbmodem1101 flash` 报 `run_serial_tool.cmake ... failed`；
  `lsof /dev/cu.usbmodem1101` 显示仍有 Python 进程占用端口（`Device or resource busy`）。

- **根因**：
  用 `idf.py monitor 2>&1 | tee xxx.log` 这种**管道方式**跑监控，停止后 `idf_monitor.py` 的 python 子进程变成孤儿进程，仍持有串口。

- **正确做法**：
  ```bash
  PID=$(lsof -t /dev/cu.usbmodem1101); [ -n "$PID" ] && kill $PID
  ```
  日常尽量用一体化命令 `idf.py -p PORT flash monitor`；单独跑 monitor 后，先确认端口已释放再 flash。

- **相关文件**：无（环境/流程问题）

---

## LL-001-3 ESP-IDF 环境缺工具，`get_idf`/`export.sh` 加载失败

- **现象**：
  - `export.sh` 报 `ERROR: tool riscv32-esp-elf has no installed versions`；
  - `idf.py` 报 `"cmake" must be available on the PATH`（cmake / ninja 未安装）。

- **根因**：
  该 IDF 安装缺少部分工具链：`cmake`/`ninja` 属于 optional，`riscv32-esp-elf` 未装；export 脚本遇到缺失工具会整体失败。

- **正确做法**：
  ```bash
  python $IDF_PATH/tools/idf_tools.py install cmake ninja riscv32-esp-elf
  ```

- **环境备注**：本机 IDF 在 **`~/opensource/esp-idf`**（`get_idf` = `. ~/opensource/esp-idf/export.sh`）。
  `AGENTS.md` 里写的 `/Volumes/MacExtStorage/projects/esp-idf-v5.5.2`、`get_idf55` 已**过时不可用**，勿按它操作。

- **相关文件**：`AGENTS.md`（描述已过时，待更新）

---

## LL-001-4 Trae 的 ESP-IDF 插件 v2.3.0 报「未找到 ESP-IDF 设置」

- **现象**：
  ESP-IDF 插件显示「未找到 ESP-IDF 设置」（`No ESP-IDF Setups found`），且无法检测串口。

- **根因**：
  插件 **v2.3.0 起移除了 `idf.espIdfPath` / `idf.toolsPath`**，改为经 EIM 或环境变量发现 IDF；而 Trae 从 Dock 启动时不加载 `.zshrc`，`IDF_PATH` 等对插件不可见。

- **正确做法**（二选一或都做）：
  1. 在 Trae 用户 `settings.json` 增加：
     ```json
     "idf.currentSetup": "/Users/blue/opensource/esp-idf",
     "idf.customExtraVars": {
       "IDF_PATH": "/Users/blue/opensource/esp-idf",
       "IDF_TOOLS_PATH": "/Users/blue/.espressif",
       "IDF_PYTHON_ENV_PATH": "/Users/blue/.espressif/python_env/idf5.5_py3.9_env"
     }
     ```
  2. 写 `~/.espressif/esp_idf.json`（插件会扫描 `IDF_TOOLS_PATH` 下该文件），内容指向 IDF / tools / python。
  改后需 `Developer: Reload Window` 让插件重新读取。

- **相关文件**：`~/Library/Application Support/Trae/User/settings.json`、`~/.espressif/esp_idf.json`

---

## LL-001-5 改了 MQTT 默认 broker 却不生效（NVS 覆盖）

- **现象**：
  改掉编译默认 `DEEP_DOG_MQTT_DEFAULT_BROKER_HOST` 后，设备仍连旧地址。

- **根因**：
  `DeepDogMqttConfig::Load()` 里 **NVS 值优先于编译默认**（`settings.GetString("broker_host", DEFAULT)`）；且已有「旧地址迁移」逻辑只覆盖了历史白名单。

- **正确做法**：
  改默认值的同时，把**旧地址加入 migrate 列表**（`mqtt_config.cc` 的 `if (s.broker_host == "...")` 分支），保证 NVS 存有旧值的设备也能切到新地址；或清 NVS 命名空间 `deep_dog_mqtt`。

- **相关文件**：`main/boards/deep-dog/mqtt/mqtt_config.h`、`main/boards/deep-dog/mqtt/mqtt_config.cc`

---

## LL-001-6 Windows 下 CMake 报 `WinError 32`（文件被占用），configure 失败

- **现象**：
  `idf.py reconfigure` / `build` 在配置阶段失败：

  ```
  CMake Error at .../tools/cmake/component.cmake:256 (message):
    PermissionError: [WinError 32]
  -- Configuring incomplete, errors occurred!
  ```

  注意它出现在**配置阶段**、且指向 `component.cmake`，容易误判为组件或 CMake 脚本错误。

- **根因**：
  Windows 文件锁：上一轮的 `idf.py monitor` / `idf.py build` 的 python 子进程尚未完全退出，
  仍占用 `build/` 目录内的文件（如 `CMakeCache.txt`、`.obj`），CMake 无法删除/重写。

- **正确做法**：
  ```powershell
  # 确认没有残留的 python / idf_monitor 进程
  Get-Process python -ErrorAction SilentlyContinue
  # 稍候几秒（等子进程真正退出）后重试，通常无需 clean
  idf.py build
  ```
  Agent 场景下，`Stop-Process` 结束 monitor 后**留出几秒**再做下一次 build；
  不要一见 `WinError 32` 就 `fullclean`（多数情况下重试即可）。

- **相关文件**：无（环境/流程问题）；与 LL-001-2（串口占用）同源，但表现是 `build/` 目录文件锁。

---

## LL-001-7 `ext_pins` 模式（PWM/CAN）决定 gimbal/servo 是否被编译启用

- **现象**：
  某个板级模块"没生效"——日志里没有它的数据点注册 / MCP 工具 / status 发布；
  启动日志可见 `ext_pins mode=can A=38 B=48 can=1 motor=1 dog=0 arm=0 servo=0 gimbal=0 led=0`。

- **根因**：
  38/48 引出脚在同一时刻只能作一种用途。`board_features.h` 在 **PWM 不可用**（如 CAN 模式）时
  强制 `DEEP_DOG_SERVO_ENABLE=0` / `DEEP_DOG_GIMBAL_ENABLE=0`，相关代码被**编译期裁剪**，
  因此运行时既无日志也无功能（不是运行时 bug）。

- **正确做法**：
  排查"某模块为何不生效"时，**先看启动日志的两行**：
  `deep_dog: ext_pins mode=... servo=? gimbal=?` 与
  `dog_mqtt: board MQTT started ... servo=? gimbal=?`。
  确认当前模式与预期一致；若确需 PWM 模式，修改 `ext_pins` 模式配置后 `idf.py reconfigure` 重编。

- **相关文件**：`main/boards/deep-dog/board_features.h`（引出脚 PWM/CAN 互斥与裁剪逻辑，约 120-130 行）、`main/boards/deep-dog/config.json`

---

## LL-001-8 板级源文件用了 `DEEP_DOG_*_ENABLE` 却没 include `config.h`，宏恒为 0

- **现象**：
  新增的 `mqtt/modules/*_datapoints.cc` 里 `#if DEEP_DOG_XXX_ENABLE` 分支**永远走不到**，
  运行期该模块数据点**静默不注册**（无编译错误、无任何日志，功能"少了一块"）。

- **根因**：
  `DEEP_DOG_*_ENABLE` 由 `board_features.h` 定义，而 `board_features.h` **只被 `config.h` include**。
  若源文件只 include 了领域头（如 `motor/motor_config.h`、`sensor/imu_config.h`）而**没** include `config.h`，
  该宏就是未定义 → C 预处理器把 `#if DEEP_DOG_XXX_ENABLE` 当作 `0`，于是走了禁用分支。

- **正确做法**：
  任何使用 `DEEP_DOG_*_ENABLE` 的板级 `.cc`，显式加：
  ```cpp
  #include "config.h"   // 提供 board_features.h 中的 DEEP_DOG_*_ENABLE
  ```
  同理，用到 `DEEP_DOG_*_AVAILABLE` 的源文件也应确保经 `config.h` 引入（`config.h` 定义 `*_AVAILABLE` 后再 include `board_features.h`）。

- **相关文件**：`main/boards/deep-dog/config.h`（第 97 行 include `board_features.h`）、`main/boards/deep-dog/board_features.h`、`main/boards/deep-dog/mqtt/modules/motor_datapoints.cc`
