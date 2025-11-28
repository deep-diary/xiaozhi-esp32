# 小智 ESP32 项目架构总览

## 1. 项目简介

小智 AI 聊天机器人是一个基于 ESP32 的智能语音交互设备，通过 MCP (Model Context Protocol) 协议实现与大模型的交互，支持多种硬件平台和通信协议。

## 2. 系统架构层次

### 2.1 整体架构分层

```
┌─────────────────────────────────────────────────────────┐
│                    应用层 (Application Layer)            │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌─────────┐ │
│  │Application│  │DeviceState│ │ McpServer │ │   OTA   │ │
│  └──────────┘  └──────────┘  └──────────┘  └─────────┘ │
├─────────────────────────────────────────────────────────┤
│                    服务层 (Service Layer)                 │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ │
│  │ AudioService │  │  Display     │  │     LED      │ │
│  └──────────────┘  └──────────────┘  └──────────────┘ │
├─────────────────────────────────────────────────────────┤
│                    协议层 (Protocol Layer)               │
│  ┌──────────────┐  ┌──────────────┐                    │
│  │WebSocketProto│  │ MqttProtocol │                    │
│  └──────────────┘  └──────────────┘                    │
├─────────────────────────────────────────────────────────┤
│                    硬件抽象层 (HAL Layer)                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ │
│  │    Board     │  │  AudioCodec  │  │   Network    │ │
│  └──────────────┘  └──────────────┘  └──────────────┘ │
├─────────────────────────────────────────────────────────┤
│                    驱动层 (Driver Layer)                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐ │
│  │   I2S/I2C   │  │    SPI/UART  │  │    GPIO     │ │
│  └──────────────┘  └──────────────┘  └──────────────┘ │
└─────────────────────────────────────────────────────────┘
```

### 2.2 核心模块说明

#### 2.2.1 应用层 (Application Layer)

**Application (主应用类)**

- 职责：系统核心控制器，协调各个模块
- 功能：
  - 设备状态管理
  - 事件循环处理
  - 协议初始化与选择
  - 音频通道管理
  - 唤醒词处理
  - OTA 升级管理

**DeviceState (设备状态)**

- 状态枚举：
  - `kDeviceStateUnknown`: 未知状态
  - `kDeviceStateStarting`: 启动中
  - `kDeviceStateWifiConfiguring`: WiFi 配置中
  - `kDeviceStateIdle`: 空闲
  - `kDeviceStateConnecting`: 连接中
  - `kDeviceStateListening`: 监听中
  - `kDeviceStateSpeaking`: 说话中
  - `kDeviceStateUpgrading`: 升级中
  - `kDeviceStateActivating`: 激活中

**McpServer (MCP 服务器)**

- 职责：实现 MCP 协议，提供设备能力发现和工具调用
- 功能：
  - 工具注册与管理
  - JSON-RPC 2.0 消息处理
  - 设备能力暴露

#### 2.2.2 服务层 (Service Layer)

**AudioService (音频服务)**

- 职责：音频处理核心服务
- 功能：
  - 音频输入/输出管理
  - Opus 编解码
  - 唤醒词检测
  - VAD (语音活动检测)
  - AEC (回声消除)
  - 音频队列管理

**Display (显示服务)**

- 职责：显示界面管理
- 支持类型：
  - OLED 显示
  - LCD 显示
  - LVGL 图形界面
  - 表情显示

**LED (LED 控制)**

- 职责：LED 状态指示
- 支持类型：
  - 单 LED
  - GPIO LED
  - 环形 LED 灯带

#### 2.2.3 协议层 (Protocol Layer)

**Protocol (协议基类)**

- 抽象接口，定义统一的协议操作
- 子类实现：
  - `WebsocketProtocol`: WebSocket 协议实现
  - `MqttProtocol`: MQTT + UDP 协议实现

**协议功能：**

- 音频通道管理
- JSON 消息传输
- 二进制音频数据传输
- 连接状态管理

#### 2.2.4 硬件抽象层 (HAL Layer)

**Board (板卡抽象)**

- 职责：硬件平台抽象，统一硬件接口
- 功能：
  - 音频编解码器获取
  - 显示设备获取
  - LED 设备获取
  - 网络接口获取
  - 电源管理
  - 电池状态
  - 温度监控

**AudioCodec (音频编解码器)**

- 职责：音频硬件接口抽象
- 功能：
  - I2S 音频输入/输出
  - 采样率配置
  - 声道配置
  - 音量控制

**NetworkInterface (网络接口)**

- 职责：网络连接抽象
- 支持类型：
  - WiFi
  - ML307 (4G Cat.1)

## 3. 数据流向

### 3.1 音频输入流 (上行)

```
麦克风 → AudioCodec → AudioInputTask → AudioProcessor
  → OpusEncoder → audio_send_queue → Protocol → 服务器
```

### 3.2 音频输出流 (下行)

```
服务器 → Protocol → audio_decode_queue → OpusDecoder
  → audio_playback_queue → AudioOutputTask → AudioCodec → 扬声器
```

### 3.3 控制消息流

```
服务器 ↔ Protocol ↔ Application ↔ McpServer ↔ 设备工具
```

## 4. 关键设计模式

### 4.1 单例模式 (Singleton)

- `Application::GetInstance()`
- `Board::GetInstance()`
- `McpServer::GetInstance()`
- `Assets::GetInstance()`

### 4.2 工厂模式 (Factory)

- `Board` 通过 `create_board()` 函数创建具体板卡实例
- 使用 `DECLARE_BOARD` 宏声明板卡

### 4.3 策略模式 (Strategy)

- `Protocol` 基类，`WebsocketProtocol` 和 `MqttProtocol` 实现不同策略
- `AudioProcessor` 支持不同的处理策略（AFE、无处理等）

### 4.4 观察者模式 (Observer)

- 通过回调函数实现事件通知
- `Protocol` 的各种回调：`OnIncomingAudio`, `OnIncomingJson` 等

## 5. 线程模型

### 5.1 主要任务

1. **main_event_loop_task**: 主事件循环，处理应用逻辑
2. **AudioInputTask**: 音频输入任务，读取麦克风数据
3. **AudioOutputTask**: 音频输出任务，播放音频数据
4. **OpusCodecTask**: Opus 编解码任务

### 5.2 任务优先级

- 主事件循环：优先级 3
- 音频任务：高优先级（实时性要求）

### 5.3 同步机制

- `EventGroup`: 事件组，用于任务间通信
- `std::mutex`: 互斥锁，保护共享资源
- `std::condition_variable`: 条件变量，用于队列等待

## 6. 存储管理

### 6.1 NVS (Non-Volatile Storage)

- 存储配置信息
- WiFi 凭证
- 设备 UUID
- 用户设置

### 6.2 SPIFFS/Partition

- 存储资源文件（Assets）
- 多语言文件
- 音频文件
- 字体文件
- 表情图片

## 7. 网络架构

### 7.1 通信协议选择

系统支持两种通信协议，根据 OTA 配置自动选择：

1. **WebSocket 协议**

   - 全双工通信
   - 文本 + 二进制混合传输
   - 适用于大多数场景

2. **MQTT + UDP 协议**
   - MQTT: 控制消息
   - UDP: 音频数据（加密）
   - 适用于对实时性要求高的场景

### 7.2 网络接口

- **WiFi**: 标准 WiFi 连接
- **ML307**: 4G Cat.1 模块，支持双网络切换

## 8. 扩展性设计

### 8.1 板卡扩展

通过实现 `Board` 接口，可以轻松添加新的硬件平台：

```cpp
class MyBoard : public Board {
    // 实现必要的虚函数
};
DECLARE_BOARD(MyBoard)
```

### 8.2 MCP 工具扩展

通过 `McpServer::AddTool()` 可以添加自定义工具：

```cpp
McpServer::GetInstance().AddTool(
    "my.tool.name",
    "Tool description",
    properties,
    callback
);
```

### 8.3 音频处理器扩展

可以实现自定义的 `AudioProcessor` 来处理音频数据。

## 9. 配置系统

### 9.1 编译时配置

通过 `Kconfig` 系统配置：

- 音频处理器选择
- 唤醒词引擎选择
- 显示类型选择
- 协议选择

### 9.2 运行时配置

通过 `Settings` 类管理：

- 网络配置
- 音频参数
- 用户偏好

## 10. 错误处理

### 10.1 错误恢复

- 网络断开自动重连
- 音频通道异常自动恢复
- OTA 失败回退机制

### 10.2 日志系统

使用 ESP-IDF 日志系统，支持不同日志级别：

- `ESP_LOGE`: 错误
- `ESP_LOGW`: 警告
- `ESP_LOGI`: 信息
- `ESP_LOGD`: 调试

## 11. 性能优化

### 11.1 音频处理

- Opus 编码复杂度可调
- 音频队列缓冲管理
- 采样率自适应

### 11.2 电源管理

- 自动进入省电模式
- 音频通道自动关闭
- 显示背光控制

### 11.3 内存管理

- 使用智能指针管理资源
- 队列大小限制
- 及时释放不需要的资源

## 12. 安全考虑

### 12.1 认证

- Bearer Token 认证
- 设备 UUID 标识
- 客户端 ID 管理

### 12.2 加密

- TLS/SSL 加密（WebSocket）
- AES-CTR 加密（UDP 音频）
- 密钥分发机制

### 12.3 防重放

- 序列号管理
- 时间戳验证
- 数据包完整性检查

## 13. 总结

小智 ESP32 项目采用了清晰的分层架构设计，通过抽象层实现了硬件无关性，通过协议层实现了通信方式的灵活性，通过服务层实现了功能的模块化。这种设计使得项目具有良好的可扩展性和可维护性，便于适配不同的硬件平台和添加新功能。
