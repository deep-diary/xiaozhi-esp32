# 小智 ESP32 — AI Agent 上下文

## 项目概述

小智 AI 聊天机器人 (xiaozhi-esp32)，基于 ESP-IDF 的语音交互终端。
通过 MCP 协议实现多端控制，支持 MQTT+UDP / WebSocket 通信。

## 技术栈

| 层 | 技术 | 版本 |
|---|------|------|
| 框架 | ESP-IDF | v5.x |
| 芯片 | ESP32-S3 (主要), ESP32-C3/C5/C6/P4 | — |
| 语言 | C/C++ (C++17) | — |
| 构建 | CMake + idf.py | — |
| 音频 | OPUS 编解码, ESP-SR 语音唤醒 | — |
| AI | Qwen / DeepSeek LLM + 流式 ASR/TTS | — |
| 协议 | MQTT+UDP, WebSocket | — |
| 显示 | OLED/LCD (LVGL) | — |

## 目录结构

```
xiaozhi-esp32/
├── main/                    # 主程序
│   ├── boards/
│   │   └── deep-dog/        # ★ 本项目定制板
│   │       ├── mqtt/        # MQTT 客户端 + 功能模块
│   │       │   └── modules/ # handle, can, motor, servo, led, imu, touch...
│   │       ├── touch_btn/   # 触摸按键
│   │       ├── servo/       # 舵机控制
│   │       ├── motor/       # 电机控制
│   │       ├── vision/      # 摄像头/RTSP
│   │       ├── sensor/      # IMU 传感器
│   │       ├── uart/        # UART 串口
│   │       ├── rs485/       # RS485 通信
│   │       └── http-server/ # HTTP 服务
│   └── audio/               # 音频引擎
├── components/              # ESP-IDF 组件
├── managed_components/      # IDF 组件管理器依赖
├── partitions/              # 分区表 (v2)
├── scripts/                 # 辅助脚本
└── docs/                    # 文档
```

## 开发环境

```bash
# === 加载 ESP-IDF 环境 ===
# 方式一：使用 alias（zsh）
get_idf55    # 加载 IDF v5.5.2 环境（xiaozhi v2.3.0 / deep-dog 项目用这个）
get_idf60    # 加载 IDF v6.0 环境（xiaozhi v2.4.0）

# 方式二：手动加载（bash/zsh 通用）
export IDF_PATH="/Volumes/MacExtStorage/projects/esp-idf-v5.5.2"
export IDF_PYTHON_ENV_PATH="$HOME/.espressif/python_env/idf5.5_py3.11_env"
. "$IDF_PATH/export.sh"

# === 设置目标芯片（首次或切换芯片时执行一次） ===
idf.py set-target esp32s3

# === 构建 ===
idf.py build

# === 烧录 + 监控（合并命令） ===
idf.py -p /dev/cu.usbmodem* flash monitor

# === 仅监控 ===
idf.py monitor
```

### 自动开发工作流（Agent 规则）

开发新功能或修改代码后，Agent 应自动执行以下流程，无需用户催促：

1. **编译**: `idf.py build` — 编译项目，修复所有编译错误
2. **下载**: `idf.py -p /dev/cu.usbmodem* flash` — 烧录到设备
3. **监控**: `idf.py monitor` — 观察启动日志，检查以下问题：
   - 是否有 `ESP_LOGE` 错误日志
   - 是否有 crash/panic/abort
   - 是否有 wifi/MQTT 连接失败
   - 是否有内存不足 (OOM) 警告
   - 功能模块初始化是否成功
4. **修复**: 如果发现上述问题，自动分析并修复代码，然后回到步骤 1
5. **验证**: 确认启动日志无异常后，告知用户结果

> **注意**: `idf.py monitor` 会持续输出日志。Agent 应在观察足够启动日志后（通常 10-15 秒），停止监控并分析输出。
> 本地开发时，idf.py 的完整路径为 `/Volumes/MacExtStorage/projects/esp-idf-v5.5.2/tools/idf.py`，环境加载后可直接使用 `idf.py`。

## 定制板: deep-dog

路径: `main/boards/deep-dog/`

- MQTT 通信为核心协议
- 功能模块通过 `swrs/mqtt/modules/` 下的 MQTT 契约定义
- 需求文档: `main/boards/deep-dog/swrs/README.md`
- 功能开关: `main/boards/deep-dog/FEATURE_FLAGS.md`

## 规范

- C++17，ESP-IDF 编码规范
- 新增 MQTT 模块遵循 `swrs/mqtt/modules/` 下的契约格式
- 固件分区表为 v2 格式 (`partitions/v2/`)
- 日志用 `ESP_LOGI`/`ESP_LOGE` 等 IDF 宏
- 配置在 `sdkconfig.defaults` / menuconfig

## 常见操作

```bash
# 清理重建
idf.py fullclean && idf.py build

# 仅构建 deep-dog 相关（增量编译自动处理）
idf.py build

# 查看串口输出
idf.py monitor

# 配置
idf.py menuconfig
```
