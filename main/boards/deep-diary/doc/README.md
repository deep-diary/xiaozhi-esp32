# ATK-DNESP32S3 开发板

## 项目概述

这是基于正点原子ESP32-S3开发板的智能控制系统，集成了多种功能模块，支持语音交互、机械臂控制、视觉识别、流媒体推送等功能。

## 目录结构

```
atk-dnesp32s3/
├── README.md                    # 本文件 - 项目总览
├── atk_dnesp32s3.cc            # 板级主文件 - 系统初始化和集成
├── config.h                     # 配置头文件 - GPIO和功能开关
├── config.json                  # 板级配置文件
│
├── motor/                       # 电机控制模块
│   ├── README.md               # 电机模块说明文档
│   ├── protocol_motor.cpp/h    # CAN协议底层
│   ├── deep_motor.cpp/h        # 电机管理器
│   ├── deep_motor_control.cc/h # MCP控制接口
│   └── deep_motor_led_state.cc/h # 电机LED状态
│
├── arm/                         # 机械臂控制模块
│   ├── README.md               # 机械臂模块说明文档
│   ├── deep_arm.cc/h           # 机械臂核心控制
│   ├── deep_arm_control.cc/h   # MCP控制接口
│   ├── arm_led_state.cc/h      # 机械臂LED状态
│   └── trajectory_planner.c/h  # 轨迹规划器
│
├── gimbal/                      # 云台控制模块
│   ├── README.md               # 云台模块说明文档
│   ├── Gimbal.c/h              # 云台底层控制
│   └── gimbal_control.cc/h     # MCP控制接口
│
├── servo/                       # 舵机控制模块
│   ├── README.md               # 舵机模块说明文档
│   └── Servo.c/h               # 舵机PWM驱动
│
├── led/                         # LED控制模块
│   ├── README.md               # LED模块说明文档
│   └── led_control.cc/h        # WS2812灯带控制
│
├── can/                         # CAN通信模块
│   ├── README.md               # CAN模块说明文档
│   └── ESP32-TWAI-CAN.cpp/hpp  # CAN总线驱动
│
├── sensor/                      # 传感器模块
│   ├── README.md               # 传感器模块说明文档
│   ├── README_QMA6100P.md      # QMA6100P详细文档
│   ├── INTEGRATION_QMA6100P.md # QMA6100P集成指南
│   └── QMA6100P/               # QMA6100P加速度计驱动
│       ├── qma6100p.c
│       └── qma6100p.h
│
├── streaming/                   # 流媒体模块
│   ├── README.md               # 流媒体模块说明文档
│   ├── mjpeg_server.cc/h       # MJPEG HTTP服务器
│   ├── http_stream.cc/h        # HTTP流媒体支持
│   ├── setup_mediamtx.sh       # MediaMTX安装脚本
│   ├── test_rtsp.sh            # RTSP测试脚本
│   ├── mediamtx-config.yml     # MediaMTX配置
│   ├── README_RTSP.md          # RTSP功能介绍
│   ├── RTSP_USAGE.md           # RTSP使用说明
│   ├── RTSP_PUSH_GUIDE.md      # RTSP推流指南
│   ├── PUSH_MODE_SUMMARY.md    # 推流模式总结
│   └── IMPLEMENTATION_SUMMARY.md # 实现总结
│
└── doc/                         # 文档目录
    ├── arm_integration_test_guide.md
    ├── arm_mcp_usage_guide.md
    ├── motor_led_usage.md
    └── ... (更多技术文档)
```

## 功能模块

### 1. 电机控制 (`motor/`)
- 基于CAN总线的电机通信
- 支持位置、速度、力矩控制
- 多电机协调管理
- 集成LED状态反馈
- MCP工具接口

**详细说明**: 参考 `motor/README.md`

### 2. 机械臂控制 (`arm/`)
- 六自由度机械臂控制
- 轨迹规划与插值
- 动作录制与播放
- NVS存储支持
- LED状态显示

**详细说明**: 参考 `arm/README.md`

### 3. 云台控制 (`gimbal/`)
- 双轴云台（水平0-270°，垂直0-180°）
- PWM舵机驱动
- CAN总线控制支持
- MCP工具接口

**详细说明**: 参考 `gimbal/README.md`

### 4. 舵机控制 (`servo/`)
- PWM信号生成
- 多舵机独立控制
- 角度范围自定义
- 支持180°/270°舵机

**详细说明**: 参考 `servo/README.md`

### 5. LED控制 (`led/`)
- WS2812可编程灯带
- 多种预设动画效果
- 状态指示功能
- MCP工具接口

**详细说明**: 参考 `led/README.md`

### 6. CAN通信 (`can/`)
- ESP32 TWAI (CAN2.0)驱动
- 标准帧和扩展帧支持
- 可配置波特率（最高1Mbps）
- 消息过滤和错误处理

**详细说明**: 参考 `can/README.md`

### 7. 传感器 (`sensor/`)
- QMA6100P三轴加速度计
- 姿态角计算（俯仰/翻滚）
- I2C通信接口
- 可扩展其他传感器

**详细说明**: 参考 `sensor/README.md`

### 8. 流媒体 (`streaming/`)
- MJPEG HTTP视频流
- 支持多客户端访问
- 可配置帧率和质量
- RTSP推流支持（通过MediaMTX）

**详细说明**: 参考 `streaming/README.md`

## 快速开始

### 1. 硬件准备

**开发板**: 正点原子 ATK-DNESP32S3
- ESP32-S3芯片
- 8MB PSRAM
- 240×280 LCD屏幕
- ES8388音频Codec
- OV2640摄像头
- XL9555 I/O扩展

**外设模块**（可选）:
- 深度电机（通过CAN）
- 云台舵机
- WS2812灯带
- QMA6100P加速度计

### 2. 软件配置

#### 配置功能开关
编辑 `config.h`:
```cpp
// 启用/禁用功能
#define ENABLE_CAMERA_FEATURE      1  // 相机功能
#define ENABLE_CAN_FEATURE         1  // CAN总线
#define ENABLE_LED_STRIP_FEATURE   1  // WS2812灯带
#define ENABLE_QMA6100P_FEATURE    1  // QMA6100P加速度计
```

#### 配置GPIO引脚
```cpp
// CAN总线
#define CAN_TX_GPIO  4
#define CAN_RX_GPIO  5

// 云台舵机
#define SERVO_PAN_GPIO   19  // 水平舵机
#define SERVO_TILT_GPIO  20  // 垂直舵机

// WS2812灯带
#define WS2812_STRIP_GPIO  8
#define WS2812_LED_COUNT   12
```

### 3. 编译和烧录

```bash
# 配置项目
idf.py menuconfig

# 编译
idf.py build

# 烧录
idf.py flash

# 监控日志
idf.py monitor
```

### 4. 基本使用

#### WiFi配置
首次启动时，设备会创建WiFi配置热点，连接后进行配置。

#### 访问视频流
```
http://<ESP32_IP>:8080/stream
```

#### MCP工具
通过MCP服务器可以控制各个模块，详见各模块README。

## 系统架构

### 初始化流程
```
main()
  ↓
atk_dnesp32s3构造函数
  ↓
InitializeI2c()          - I2C总线初始化
  ↓
InitializeSpi()          - SPI总线初始化
  ↓
InitializeSt7789Display() - LCD显示屏
  ↓
InitializeCamera()       - OV2640摄像头
  ↓
InitializeGimbal()       - 云台舵机
  ↓
InitializeWs2812()       - WS2812灯带
  ↓
InitializeCan()          - CAN总线和电机
  ↓
InitializeControls()     - MCP控制接口
  ↓
InitializeQMA6100P()     - 加速度计
  ↓
StartNetwork()           - WiFi和MJPEG服务器
```

### 任务架构
- **主任务**: 系统初始化和事件处理
- **CAN接收任务**: 处理电机反馈和CAN指令
- **用户主循环**: 传感器数据采集和显示
- **HTTP服务器任务**: MJPEG视频流推送
- **音频任务**: 语音交互（由框架管理）

### 模块依赖关系
```
          [atk_dnesp32s3.cc 主文件]
                    |
    +---------------+---------------+
    |               |               |
  [电机]          [机械臂]        [云台]
    |               |               |
    +-------+-------+-------+-------+
            |               |
          [CAN]           [舵机]
            
  [LED]  [传感器]  [流媒体]
```

## 开发指南

### 添加新功能

1. **在相应模块目录下添加代码**
   - 例如：新的传感器驱动添加到 `sensor/`

2. **更新模块README**
   - 说明新功能的用途和使用方法

3. **在主文件中集成**
   - 添加include
   - 在构造函数中初始化
   - 必要时创建MCP工具

4. **更新配置文件**
   - 在 `config.h` 添加必要的宏定义

### 代码规范

- **C++代码**: 使用 `.cc` 和 `.h` 扩展名
- **C代码**: 使用 `.c` 和 `.h` 扩展名
- **命名**: 使用驼峰命名法（类）或下划线命名法（C函数）
- **注释**: 使用中文注释说明关键逻辑
- **日志**: 使用ESP_LOGI/W/E进行日志输出

### 调试技巧

#### 启用详细日志
```cpp
esp_log_level_set("atk_dnesp32s3", ESP_LOG_DEBUG);
esp_log_level_set("DeepMotor", ESP_LOG_DEBUG);
// ... 其他模块
```

#### 监控资源使用
```cpp
// 打印堆内存使用
ESP_LOGI(TAG, "Free heap: %lu", esp_get_free_heap_size());

// 打印任务栈使用
vTaskList(buffer);
ESP_LOGI(TAG, "Tasks:\n%s", buffer);
```

## 性能参数

- **CPU频率**: 240MHz（双核）
- **内存**: 512KB SRAM + 8MB PSRAM
- **Flash**: 16MB
- **WiFi**: 802.11 b/g/n (2.4GHz)
- **CAN波特率**: 最高1Mbps
- **视频帧率**: 最高25fps (QVGA)
- **音频采样率**: 16kHz输入 / 48kHz输出

## 故障排查

### 常见问题

**1. 编译错误**
- 检查ESP-IDF版本（推荐v5.x）
- 确认所有依赖组件已安装
- 清理构建：`idf.py fullclean`

**2. 摄像头初始化失败**
- 检查摄像头连接
- 确认XL9555 I/O扩展工作正常
- 查看电源是否稳定

**3. CAN通信失败**
- 检查CAN收发器连接
- 确认终端电阻（120Ω）
- 验证波特率匹配

**4. 视频流无法访问**
- 确认WiFi已连接
- 检查IP地址
- 验证防火墙设置

## 更新日志

### v1.0.0 (当前版本)
- ✅ 模块化代码结构
- ✅ 电机控制集成
- ✅ 机械臂控制
- ✅ 云台控制
- ✅ 视频流推送
- ✅ QMA6100P加速度计
- ✅ MCP工具接口
- ✅ 完整的文档

### 计划中的功能
- [ ] 更多传感器支持
- [ ] H.264视频编码
- [ ] 云端数据同步
- [ ] OTA升级优化
- [ ] 机器学习推理

## 贡献指南

欢迎提交问题和改进建议！

1. Fork项目
2. 创建功能分支
3. 提交更改
4. 发起Pull Request

## 许可证

参考项目根目录的LICENSE文件

## 联系方式

- 项目地址: [GitHub仓库]
- 问题反馈: [Issues页面]
- 技术文档: `doc/` 目录

## 致谢

- ESP-IDF框架
- 正点原子开发板
- 社区贡献者

---

**最后更新**: 2025年10月
**维护者**: DeepDiary团队

