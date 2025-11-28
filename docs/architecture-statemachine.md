# 状态机流程图

本文档详细描述小智 ESP32 项目中各种状态机的状态转换和触发条件。

## 1. 设备主状态机

### 1.1 完整状态转换图

```mermaid
stateDiagram-v2
    [*] --> Unknown: 系统启动
    
    Unknown --> Starting: 初始化开始
    
    Starting --> WifiConfiguring: 需要配置WiFi
    Starting --> Activating: 网络就绪
    
    WifiConfiguring --> Activating: WiFi配置完成
    WifiConfiguring --> Idle: 配置超时/取消
    
    Activating --> Upgrading: 检测到新版本
    Activating --> Idle: 激活完成/无需激活
    
    Upgrading --> [*]: 升级完成重启
    Upgrading --> Idle: 升级失败
    
    Idle --> Connecting: 用户触发<br/>(唤醒词/按键)
    Idle --> AudioTesting: 音频测试模式
    
    Connecting --> Idle: 连接失败
    Connecting --> Listening: 连接成功
    
    Listening --> Speaking: 收到TTS开始
    Listening --> Idle: 手动停止/超时
    
    Speaking --> Listening: TTS结束(自动模式)
    Speaking --> Idle: TTS结束(手动模式)
    Speaking --> Listening: 检测到唤醒词中断
    
    AudioTesting --> WifiConfiguring: 测试结束
    
    Idle --> FatalError: 严重错误
    Listening --> FatalError: 严重错误
    Speaking --> FatalError: 严重错误
    FatalError --> [*]: 重启
    
    note right of Idle
        空闲状态:
        - 启用唤醒词检测
        - 禁用语音处理
        - 显示待机界面
    end note
    
    note right of Listening
        监听状态:
        - 禁用唤醒词检测
        - 启用语音处理
        - 发送音频到服务器
        - 显示监听界面
    end note
    
    note right of Speaking
        说话状态:
        - 停止发送音频
        - 播放TTS音频
        - 显示说话界面
    end note
```

### 1.2 状态详细说明

#### Unknown (未知状态)
- **进入条件**: 系统启动
- **功能**: 初始状态，等待初始化
- **退出条件**: 开始初始化流程

#### Starting (启动中)
- **进入条件**: 开始初始化
- **功能**: 
  - 初始化硬件
  - 初始化音频服务
  - 初始化显示
  - 初始化网络
- **退出条件**: 
  - 需要配置 WiFi → `WifiConfiguring`
  - 网络就绪 → `Activating`

#### WifiConfiguring (WiFi配置中)
- **进入条件**: 需要配置 WiFi
- **功能**:
  - 进入 WiFi AP 模式
  - 提供配置界面
  - 等待用户配置
- **退出条件**:
  - 配置完成 → `Activating`
  - 超时/取消 → `Idle`

#### Activating (激活中)
- **进入条件**: 网络就绪
- **功能**:
  - 检查新版本
  - 显示激活码（如需要）
  - 等待激活确认
- **退出条件**:
  - 检测到新版本 → `Upgrading`
  - 激活完成/无需激活 → `Idle`

#### Upgrading (升级中)
- **进入条件**: 检测到新版本
- **功能**:
  - 下载固件
  - 验证固件
  - 写入 OTA 分区
  - 重启设备
- **退出条件**:
  - 升级成功 → 重启（回到 `Unknown`）
  - 升级失败 → `Idle`

#### Idle (空闲)
- **进入条件**: 
  - 激活完成
  - 连接失败
  - 会话结束
- **功能**:
  - 启用唤醒词检测
  - 禁用语音处理
  - 显示待机界面
  - 等待用户操作
- **退出条件**:
  - 用户触发 → `Connecting`
  - 音频测试 → `AudioTesting`

#### Connecting (连接中)
- **进入条件**: 用户触发（唤醒词/按键）
- **功能**:
  - 打开音频通道
  - 建立网络连接
  - 发送 Hello 消息
- **退出条件**:
  - 连接成功 → `Listening`
  - 连接失败 → `Idle`

#### Listening (监听中)
- **进入条件**: 音频通道打开成功
- **功能**:
  - 禁用唤醒词检测
  - 启用语音处理
  - 发送音频数据到服务器
  - 发送 listen 消息
  - 显示监听界面
- **退出条件**:
  - 收到 TTS 开始 → `Speaking`
  - 手动停止 → `Idle`
  - 超时 → `Idle`

#### Speaking (说话中)
- **进入条件**: 收到 TTS 开始消息
- **功能**:
  - 停止发送音频
  - 播放 TTS 音频
  - 显示说话界面
- **退出条件**:
  - TTS 结束（自动模式）→ `Listening`
  - TTS 结束（手动模式）→ `Idle`
  - 检测到唤醒词中断 → `Listening`

#### AudioTesting (音频测试)
- **进入条件**: 进入音频测试模式
- **功能**:
  - 测试音频输入/输出
  - 显示测试界面
- **退出条件**: 测试结束 → `WifiConfiguring`

#### FatalError (致命错误)
- **进入条件**: 发生严重错误
- **功能**:
  - 显示错误信息
  - 记录错误日志
- **退出条件**: 重启设备

## 2. 协议连接状态机

### 2.1 WebSocket 协议状态机

```mermaid
stateDiagram-v2
    [*] --> Disconnected: 初始状态
    
    Disconnected --> Connecting: OpenAudioChannel()
    
    Connecting --> Connected: WebSocket连接成功
    Connecting --> Disconnected: 连接失败
    
    Connected --> HelloSent: 发送Hello消息
    
    HelloSent --> WaitingHello: 等待服务器响应
    
    WaitingHello --> ChannelOpened: 收到Hello响应
    WaitingHello --> Disconnected: 超时/错误
    
    ChannelOpened --> Streaming: 开始传输数据
    
    Streaming --> ChannelOpened: 暂停传输
    Streaming --> Disconnected: 连接断开
    
    ChannelOpened --> Disconnected: CloseAudioChannel()
    
    Disconnected --> [*]: 清理资源
    
    note right of ChannelOpened
        音频通道已打开:
        - 可以发送音频
        - 可以接收音频
        - 可以发送控制消息
    end note
```

### 2.2 MQTT + UDP 协议状态机

```mermaid
stateDiagram-v2
    [*] --> Disconnected: 初始状态
    
    Disconnected --> MqttConnecting: OpenAudioChannel()
    
    MqttConnecting --> MqttConnected: MQTT连接成功
    MqttConnecting --> Disconnected: MQTT连接失败
    
    MqttConnected --> RequestingChannel: 发送Hello消息
    
    RequestingChannel --> ChannelOpened: 收到Hello响应<br/>(包含UDP信息)
    RequestingChannel --> MqttConnected: 超时/错误
    
    ChannelOpened --> UdpConnecting: 建立UDP连接
    
    UdpConnecting --> UdpConnected: UDP连接成功
    UdpConnecting --> ChannelOpened: UDP连接失败<br/>(可重试)
    
    UdpConnected --> Streaming: 开始传输数据
    
    Streaming --> UdpConnected: 暂停传输
    Streaming --> UdpConnected: UDP断开<br/>(可重连)
    
    UdpConnected --> ChannelOpened: UDP断开
    ChannelOpened --> MqttConnected: 关闭音频通道
    
    MqttConnected --> Disconnected: MQTT断开
    
    Disconnected --> [*]: 清理资源
    
    note right of UdpConnected
        音频通道已打开:
        - MQTT: 控制消息
        - UDP: 音频数据(加密)
    end note
```

## 3. 音频服务状态机

### 3.1 音频处理状态

```mermaid
stateDiagram-v2
    [*] --> Idle: 初始化
    
    Idle --> WakeWordOnly: 启用唤醒词检测
    
    WakeWordOnly --> VoiceProcessing: 开始监听
    WakeWordOnly --> Idle: 禁用唤醒词
    
    VoiceProcessing --> WakeWordOnly: 停止监听
    VoiceProcessing --> Playback: 开始播放
    
    Playback --> VoiceProcessing: 播放结束(自动模式)
    Playback --> WakeWordOnly: 播放结束(手动模式)
    Playback --> WakeWordOnly: 播放中断
    
    note right of WakeWordOnly
        仅唤醒词检测:
        - AudioInputTask运行
        - WakeWord引擎运行
        - AudioProcessor停止
    end note
    
    note right of VoiceProcessing
        语音处理:
        - AudioInputTask运行
        - AudioProcessor运行
        - Opus编码运行
        - 发送音频到服务器
    end note
    
    note right of Playback
        播放中:
        - AudioOutputTask运行
        - Opus解码运行
        - 接收音频从服务器
        - 播放到扬声器
    end note
```

### 3.2 音频通道状态

```mermaid
stateDiagram-v2
    [*] --> Disabled: 初始状态
    
    Disabled --> Enabling: EnableInput(true)<br/>或EnableOutput(true)
    
    Enabling --> Enabled: 硬件就绪
    
    Enabled --> Disabling: 超时无活动<br/>或显式禁用
    
    Disabling --> Disabled: 硬件关闭
    
    note right of Enabled
        通道启用:
        - 输入: ADC开启
        - 输出: DAC开启
        - 功耗较高
    end note
    
    note right of Disabled
        通道禁用:
        - 输入: ADC关闭
        - 输出: DAC关闭
        - 省电模式
    end note
```

## 4. 网络连接状态机

### 4.1 WiFi 连接状态

```mermaid
stateDiagram-v2
    [*] --> Disconnected: 初始状态
    
    Disconnected --> Connecting: StartNetwork()
    
    Connecting --> Connected: 连接成功
    Connecting --> ConfigMode: 连接失败<br/>(无配置)
    Connecting --> Disconnected: 连接失败<br/>(有配置但失败)
    
    Connected --> Disconnected: 断开连接
    Connected --> Reconnecting: 网络异常
    
    Reconnecting --> Connected: 重连成功
    Reconnecting --> Disconnected: 重连失败
    
    ConfigMode --> Connected: 配置完成
    ConfigMode --> Disconnected: 配置超时
    
    note right of ConfigMode
        WiFi配置模式:
        - AP模式
        - 提供配置页面
        - 等待用户配置
    end note
```

### 4.2 ML307 4G 连接状态

```mermaid
stateDiagram-v2
    [*] --> Disconnected: 初始状态
    
    Disconnected --> Initializing: StartNetwork()
    
    Initializing --> Registering: 初始化完成
    
    Registering --> Registered: 注册成功
    Registering --> Disconnected: 注册失败
    
    Registered --> Connected: 获取IP地址
    
    Connected --> Disconnected: 断开连接
    Connected --> Reconnecting: 网络异常
    
    Reconnecting --> Connected: 重连成功
    Reconnecting --> Disconnected: 重连失败
    
    note right of Registered
        已注册到网络:
        - 信号强度正常
        - 等待IP分配
    end note
```

## 5. MCP 会话状态机

```mermaid
stateDiagram-v2
    [*] --> Uninitialized: 初始状态
    
    Uninitialized --> Initializing: 收到initialize请求
    
    Initializing --> Initialized: 发送initialize响应
    
    Initialized --> Ready: 会话就绪
    
    Ready --> Processing: 收到tools/call请求
    
    Processing --> Executing: 解析参数
    
    Executing --> Responding: 执行工具
    
    Responding --> Ready: 发送响应
    
    Processing --> Error: 解析失败
    Executing --> Error: 执行失败
    
    Error --> Ready: 发送错误响应
    
    Ready --> Uninitialized: 会话结束
    
    note right of Ready
        就绪状态:
        - 可以接收工具调用
        - 可以发送通知
        - 可以响应工具列表请求
    end note
```

## 6. 唤醒词检测状态机

```mermaid
stateDiagram-v2
    [*] --> Idle: 初始化
    
    Idle --> Detecting: 启用检测
    
    Detecting --> Processing: 检测到关键词
    
    Processing --> Confirming: 确认唤醒词
    
    Confirming --> Detected: 确认成功
    Confirming --> Detecting: 确认失败
    
    Detected --> Idle: 触发回调
    
    Detecting --> Idle: 禁用检测
    
    note right of Detecting
        检测中:
        - 持续分析音频
        - 匹配唤醒词模型
        - 低功耗模式
    end note
    
    note right of Detected
        检测成功:
        - 触发Application回调
        - 开始语音交互
    end note
```

## 7. OTA 升级状态机

```mermaid
stateDiagram-v2
    [*] --> Idle: 初始状态
    
    Idle --> Checking: CheckVersion()
    
    Checking --> HasNewVersion: 检测到新版本
    Checking --> NoNewVersion: 无新版本
    
    NoNewVersion --> Idle: 继续运行
    
    HasNewVersion --> Downloading: 开始下载
    
    Downloading --> Verifying: 下载完成
    Downloading --> Failed: 下载失败
    
    Verifying --> Writing: 验证通过
    Verifying --> Failed: 验证失败
    
    Writing --> Rebooting: 写入完成
    
    Rebooting --> [*]: 设备重启
    
    Failed --> Idle: 继续运行旧版本
    
    note right of Downloading
        下载中:
        - 显示下载进度
        - 支持断点续传
        - 超时重试
    end note
    
    note right of Verifying
        验证中:
        - 校验文件完整性
        - 验证签名
        - 检查版本号
    end note
```

## 8. 电源管理状态机

```mermaid
stateDiagram-v2
    [*] --> Normal: 正常模式
    
    Normal --> PowerSaving: 无活动超时
    
    PowerSaving --> Normal: 检测到活动
    
    Normal --> Sleep: 进入睡眠模式<br/>(可配置)
    
    Sleep --> Normal: 唤醒事件
    
    note right of PowerSaving
        省电模式:
        - 降低CPU频率
        - 关闭不必要外设
        - 保持基本功能
    end note
    
    note right of Sleep
        睡眠模式:
        - 深度睡眠
        - 仅保留唤醒源
        - 最低功耗
    end note
```

## 9. 状态转换表

### 9.1 设备主状态转换表

| 当前状态 | 事件 | 下一状态 | 动作 |
|---------|------|---------|------|
| Unknown | 初始化开始 | Starting | 初始化硬件 |
| Starting | 需要WiFi配置 | WifiConfiguring | 进入AP模式 |
| Starting | 网络就绪 | Activating | 检查版本 |
| WifiConfiguring | 配置完成 | Activating | 连接WiFi |
| Activating | 检测到新版本 | Upgrading | 下载固件 |
| Activating | 激活完成 | Idle | 初始化协议 |
| Idle | 用户触发 | Connecting | 打开音频通道 |
| Connecting | 连接成功 | Listening | 开始监听 |
| Listening | 收到TTS | Speaking | 停止发送音频 |
| Speaking | TTS结束(自动) | Listening | 继续监听 |
| Speaking | TTS结束(手动) | Idle | 停止会话 |

### 9.2 协议状态转换表

| 当前状态 | 事件 | 下一状态 | 动作 |
|---------|------|---------|------|
| Disconnected | OpenAudioChannel() | Connecting | 建立连接 |
| Connecting | 连接成功 | Connected | 发送Hello |
| Connected | 收到Hello响应 | ChannelOpened | 标记就绪 |
| ChannelOpened | 开始传输 | Streaming | 发送/接收数据 |
| Streaming | 连接断开 | Disconnected | 清理资源 |

## 10. 状态同步机制

### 10.1 状态通知

- **DeviceStateEventManager**: 管理状态变化事件
- **回调函数**: 各模块注册状态变化回调
- **EventGroup**: 任务间状态同步

### 10.2 状态持久化

- **NVS存储**: 保存关键状态（如WiFi配置）
- **运行时状态**: 仅在内存中，重启后重置

### 10.3 状态恢复

- **网络断开**: 自动重连，恢复连接状态
- **音频通道异常**: 自动关闭，回到Idle状态
- **严重错误**: 记录日志，重启设备

## 11. 状态机设计原则

### 11.1 状态定义

- **明确性**: 每个状态有明确的含义和功能
- **互斥性**: 同一时刻只能处于一个状态
- **完整性**: 覆盖所有可能的状态

### 11.2 转换规则

- **确定性**: 相同条件下总是转换到相同状态
- **可追溯**: 状态转换有明确的触发条件
- **可恢复**: 异常情况下能恢复到安全状态

### 11.3 状态保护

- **状态检查**: 转换前检查当前状态
- **并发控制**: 使用互斥锁保护状态变量
- **原子操作**: 状态转换是原子性的

## 12. 总结

状态机设计确保了系统行为的可预测性和稳定性：

1. **清晰的状态定义**: 每个状态都有明确的职责
2. **完整的转换路径**: 覆盖所有可能的转换场景
3. **异常处理**: 异常情况下能安全恢复
4. **状态同步**: 多模块间状态保持一致
5. **可扩展性**: 易于添加新状态和转换

这种设计使得系统在各种情况下都能保持稳定运行。

