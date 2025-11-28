# 模块交互序列图

本文档使用序列图展示小智 ESP32 项目中关键场景下各模块之间的交互流程。

## 1. 系统启动序列

```mermaid
sequenceDiagram
    participant Main as main()
    participant App as Application
    participant Board as Board
    participant Display as Display
    participant Audio as AudioService
    participant Codec as AudioCodec
    participant Network as NetworkInterface
    participant Protocol as Protocol
    participant MCP as McpServer

    Main->>App: GetInstance()
    Main->>App: Start()
    
    App->>App: SetDeviceState(Starting)
    App->>Display: SetChatMessage("system", user_agent)
    
    App->>Audio: Initialize(codec)
    Audio->>Codec: Start()
    Audio->>Audio: 初始化Opus编解码器
    Audio->>Audio: 初始化AudioProcessor
    Audio->>Audio: 初始化WakeWord
    Audio->>Audio: Start()
    
    App->>App: 创建主事件循环任务
    
    App->>Board: StartNetwork()
    Board->>Network: Connect()
    Network-->>Board: 连接成功
    
    App->>App: CheckAssetsVersion()
    App->>App: CheckNewVersion()
    
    App->>MCP: AddCommonTools()
    App->>MCP: AddUserOnlyTools()
    
    App->>App: 选择协议类型
    alt MQTT配置存在
        App->>Protocol: new MqttProtocol()
    else WebSocket配置存在
        App->>Protocol: new WebsocketProtocol()
    end
    
    App->>Protocol: Start()
    Protocol->>Network: 建立连接
    
    App->>App: SetDeviceState(Idle)
```

## 2. 唤醒词触发交互序列

```mermaid
sequenceDiagram
    participant User as 用户
    participant Mic as 麦克风
    participant Codec as AudioCodec
    participant AudioInput as AudioInputTask
    participant WakeWord as WakeWord引擎
    participant App as Application
    participant Protocol as Protocol
    participant Server as 服务器

    loop 持续监听
        Mic->>Codec: I2S音频数据
        Codec->>AudioInput: ReadAudioData()
        AudioInput->>WakeWord: Feed(audio_data)
        WakeWord->>WakeWord: 检测唤醒词
    end
    
    WakeWord->>WakeWord: 检测到唤醒词
    WakeWord->>App: OnWakeWordDetected("你好小明")
    
    App->>App: WakeWordInvoke("你好小明")
    App->>App: SetDeviceState(Connecting)
    
    App->>Protocol: OpenAudioChannel()
    Protocol->>Server: 建立连接
    Server-->>Protocol: 连接成功
    
    Protocol->>Server: Hello消息
    Server-->>Protocol: Hello响应(session_id)
    
    Protocol->>App: OnAudioChannelOpened()
    App->>App: SetDeviceState(Listening)
    
    App->>Protocol: SendStartListening(auto)
    App->>Audio: EnableVoiceProcessing(true)
    App->>Audio: EnableWakeWordDetection(false)
    
    Note over App,Server: 开始发送音频数据
```

## 3. 语音交互完整序列

```mermaid
sequenceDiagram
    participant User as 用户
    participant Audio as AudioService
    participant Protocol as Protocol
    participant Server as 服务器
    participant App as Application
    participant Display as Display

    Note over User,Display: 1. 用户说话
    User->>Audio: 语音输入
    Audio->>Audio: 音频处理(AEC/VAD)
    Audio->>Audio: Opus编码
    Audio->>Protocol: SendAudio(packet)
    Protocol->>Server: 发送Opus音频数据
    
    Note over User,Display: 2. 服务器识别
    Server->>Protocol: STT消息{"type":"stt","text":"..."}
    Protocol->>App: OnIncomingJson(STT)
    App->>Display: SetChatMessage("user", text)
    
    Note over User,Display: 3. 服务器生成回复
    Server->>Protocol: TTS开始{"type":"tts","state":"start"}
    Protocol->>App: OnIncomingJson(TTS)
    App->>App: SetDeviceState(Speaking)
    App->>Audio: 停止发送音频
    
    Note over User,Display: 4. 服务器发送TTS音频
    Server->>Protocol: Opus音频数据
    Protocol->>App: OnIncomingAudio(packet)
    App->>Audio: PushPacketToDecodeQueue(packet)
    Audio->>Audio: Opus解码
    Audio->>User: 播放音频
    
    Note over User,Display: 5. TTS结束
    Server->>Protocol: TTS结束{"type":"tts","state":"stop"}
    Protocol->>App: OnIncomingJson(TTS)
    App->>App: SetDeviceState(Listening)
    App->>Audio: 继续发送音频(自动模式)
```

## 4. MCP 工具调用序列

```mermaid
sequenceDiagram
    participant Server as 服务器
    participant Protocol as Protocol
    participant App as Application
    participant MCP as McpServer
    participant Tool as McpTool
    participant Board as Board
    participant Hardware as 硬件设备

    Server->>Protocol: MCP消息{"type":"mcp","payload":{...}}
    Protocol->>App: OnIncomingJson(MCP)
    App->>MCP: ParseMessage(json)
    
    MCP->>MCP: 解析JSON-RPC消息
    MCP->>MCP: 提取method和params
    
    alt method == "tools/list"
        MCP->>MCP: GetToolsList(cursor)
        MCP->>MCP: 构建工具列表JSON
        MCP->>App: SendMcpMessage(result)
        App->>Protocol: SendMcpMessage(result)
        Protocol->>Server: 发送工具列表
    else method == "tools/call"
        MCP->>MCP: DoToolCall(name, arguments)
        MCP->>Tool: 查找工具
        MCP->>Tool: Call(PropertyList)
        Tool->>Board: 调用硬件接口
        Board->>Hardware: 执行操作
        Hardware-->>Board: 返回结果
        Board-->>Tool: 返回结果
        Tool-->>MCP: 返回结果JSON
        MCP->>App: SendMcpMessage(result)
        App->>Protocol: SendMcpMessage(result)
        Protocol->>Server: 发送执行结果
    else method == "initialize"
        MCP->>MCP: 解析capabilities
        MCP->>MCP: 构建初始化响应
        MCP->>App: SendMcpMessage(result)
        App->>Protocol: SendMcpMessage(result)
        Protocol->>Server: 发送初始化响应
    end
```

## 5. WebSocket 连接建立序列

```mermaid
sequenceDiagram
    participant App as Application
    participant Protocol as WebsocketProtocol
    participant WS as WebSocket
    participant Server as 服务器

    App->>Protocol: OpenAudioChannel()
    Protocol->>Protocol: 构建WebSocket URL
    Protocol->>Protocol: 设置请求头<br/>(Authorization, Device-Id, etc.)
    
    Protocol->>WS: Connect(url, headers)
    WS->>Server: WebSocket握手请求
    Server-->>WS: 101 Switching Protocols
    WS-->>Protocol: OnConnected()
    
    Protocol->>Protocol: 构建Hello消息
    Protocol->>WS: SendText(hello_json)
    WS->>Server: 发送Hello消息
    
    Server->>WS: Hello响应
    WS-->>Protocol: OnData(text_frame)
    Protocol->>Protocol: 解析Hello响应
    Protocol->>Protocol: 提取session_id
    Protocol->>Protocol: 提取audio_params
    
    Protocol->>App: OnAudioChannelOpened()
    App->>App: 标记音频通道就绪
    
    Note over App,Server: 音频通道已打开，可以传输数据
```

## 6. MQTT + UDP 连接建立序列

```mermaid
sequenceDiagram
    participant App as Application
    participant Protocol as MqttProtocol
    participant MQTT as MqttClient
    participant UDP as UdpClient
    participant Server as 服务器

    App->>Protocol: OpenAudioChannel()
    
    Protocol->>MQTT: Connect(broker, client_id, ...)
    MQTT->>Server: MQTT连接请求
    Server-->>MQTT: CONNACK
    MQTT-->>Protocol: OnConnected()
    
    Protocol->>Protocol: 构建Hello消息
    Protocol->>MQTT: Publish(hello_json)
    MQTT->>Server: 发送Hello消息
    
    Server->>MQTT: Hello响应(包含UDP信息)
    MQTT-->>Protocol: OnMessage(hello_response)
    Protocol->>Protocol: 解析Hello响应
    Protocol->>Protocol: 提取UDP配置<br/>(server, port, key, nonce)
    
    Protocol->>UDP: 初始化AES加密上下文
    Protocol->>UDP: Connect(server, port)
    UDP->>Server: UDP连接
    Server-->>UDP: 连接成功
    UDP-->>Protocol: OnConnected()
    
    Protocol->>App: OnAudioChannelOpened()
    App->>App: 标记音频通道就绪
    
    Note over App,Server: MQTT用于控制，UDP用于音频
```

## 7. 音频数据传输序列（WebSocket）

```mermaid
sequenceDiagram
    participant Mic as 麦克风
    participant Audio as AudioService
    participant App as Application
    participant Protocol as WebsocketProtocol
    participant WS as WebSocket
    participant Server as 服务器

    loop 持续录音
        Mic->>Audio: I2S音频数据
        Audio->>Audio: 音频处理
        Audio->>Audio: Opus编码
        Audio->>App: PopPacketFromSendQueue()
        App->>Protocol: SendAudio(packet)
        
        alt 协议版本1
            Protocol->>WS: SendBinary(opus_data)
        else 协议版本2
            Protocol->>Protocol: 构建BinaryProtocol2<br/>(timestamp, payload)
            Protocol->>WS: SendBinary(protocol_data)
        else 协议版本3
            Protocol->>Protocol: 构建BinaryProtocol3<br/>(payload)
            Protocol->>WS: SendBinary(protocol_data)
        end
        
        WS->>Server: 发送二进制帧
    end
    
    loop 接收TTS音频
        Server->>WS: 发送二进制帧(Opus)
        WS-->>Protocol: OnData(binary_frame)
        Protocol->>Protocol: 解析音频数据
        Protocol->>App: OnIncomingAudio(packet)
        App->>Audio: PushPacketToDecodeQueue(packet)
        Audio->>Audio: Opus解码
        Audio->>Mic: 播放音频(扬声器)
    end
```

## 8. 音频数据传输序列（MQTT + UDP）

```mermaid
sequenceDiagram
    participant Mic as 麦克风
    participant Audio as AudioService
    participant App as Application
    participant Protocol as MqttProtocol
    participant UDP as UdpClient
    participant Server as 服务器

    loop 持续录音
        Mic->>Audio: I2S音频数据
        Audio->>Audio: 音频处理
        Audio->>Audio: Opus编码
        Audio->>App: PopPacketFromSendQueue()
        App->>Protocol: SendAudio(packet)
        
        Protocol->>Protocol: 构建UDP数据包<br/>(type, flags, ssrc, timestamp, sequence)
        Protocol->>Protocol: AES-CTR加密
        Protocol->>UDP: Send(encrypted_data)
        UDP->>Server: 发送UDP数据包
        
        Protocol->>Protocol: 更新local_sequence
    end
    
    loop 接收TTS音频
        Server->>UDP: 发送UDP数据包(加密)
        UDP-->>Protocol: OnData(encrypted_data)
        Protocol->>Protocol: 验证序列号
        Protocol->>Protocol: AES-CTR解密
        Protocol->>Protocol: 解析音频数据
        Protocol->>App: OnIncomingAudio(packet)
        App->>Audio: PushPacketToDecodeQueue(packet)
        Audio->>Audio: Opus解码
        Audio->>Mic: 播放音频(扬声器)
        
        Protocol->>Protocol: 更新remote_sequence
    end
```

## 9. OTA 升级序列

```mermaid
sequenceDiagram
    participant App as Application
    participant OTA as Ota
    participant Network as NetworkInterface
    participant Server as 服务器
    participant Flash as Flash存储

    App->>OTA: CheckVersion()
    OTA->>Network: HTTP GET(check_version_url)
    Network->>Server: 请求版本信息
    Server-->>Network: 返回版本信息
    Network-->>OTA: 版本信息
    
    OTA->>OTA: 比较版本号
    OTA-->>App: HasNewVersion() = true
    
    App->>App: SetDeviceState(Upgrading)
    App->>App: UpgradeFirmware(ota)
    
    OTA->>Network: HTTP GET(firmware_url)
    Network->>Server: 请求固件文件
    Server-->>Network: 流式传输固件
    Network-->>OTA: 固件数据块
    
    loop 下载固件
        OTA->>Flash: 写入OTA分区
        OTA->>App: 更新下载进度
        App->>App: 显示进度
    end
    
    OTA->>OTA: 验证固件完整性
    OTA->>OTA: 验证固件签名
    
    alt 验证成功
        OTA->>Flash: 设置启动分区
        OTA->>App: 升级成功
        App->>App: 重启设备
    else 验证失败
        OTA->>App: 升级失败
        App->>App: SetDeviceState(Idle)
    end
```

## 10. 资源文件下载序列

```mermaid
sequenceDiagram
    participant App as Application
    participant Settings as Settings
    participant Assets as Assets
    participant Network as NetworkInterface
    participant Server as 服务器
    participant SPIFFS as SPIFFS分区

    App->>App: CheckAssetsVersion()
    App->>Settings: GetString("download_url")
    Settings-->>App: download_url
    
    alt 有下载URL
        App->>App: SetDeviceState(Upgrading)
        App->>App: Alert("加载资源")
        App->>Assets: Download(url, progress_callback)
        
        Assets->>Network: HTTP GET(url)
        Network->>Server: 请求资源文件
        Server-->>Network: 流式传输资源
        Network-->>Assets: 资源数据块
        
        loop 下载资源
            Assets->>SPIFFS: 写入资源文件
            Assets->>App: progress_callback(progress, speed)
            App->>App: 显示下载进度
        end
        
        Assets-->>App: Download() = true
        App->>Assets: Apply()
        Assets->>SPIFFS: 加载资源文件
        Assets->>Assets: 解析资源文件
        Assets->>App: 资源加载完成
    else 无下载URL
        App->>Assets: Apply()
        Assets->>SPIFFS: 加载本地资源
    end
```

## 11. 设备状态变化通知序列

```mermaid
sequenceDiagram
    participant App as Application
    participant StateMgr as DeviceStateEventManager
    participant Display as Display
    participant LED as Led
    participant Audio as AudioService
    participant Listener1 as 监听器1
    participant Listener2 as 监听器2

    App->>App: SetDeviceState(new_state)
    App->>StateMgr: PostStateChangeEvent(old_state, new_state)
    
    StateMgr->>Display: 状态变化通知
    Display->>Display: 更新界面显示
    
    StateMgr->>LED: 状态变化通知
    LED->>LED: OnStateChanged()
    LED->>LED: 更新LED状态
    
    StateMgr->>Audio: 状态变化通知
    Audio->>Audio: 根据状态调整音频处理
    
    StateMgr->>Listener1: 状态变化回调
    StateMgr->>Listener2: 状态变化回调
    
    Note over App,Listener2: 所有注册的监听器都会收到通知
```

## 12. 错误处理序列

```mermaid
sequenceDiagram
    participant Protocol as Protocol
    participant App as Application
    participant Display as Display
    participant Network as NetworkInterface

    Protocol->>Protocol: 检测到网络错误
    Protocol->>Protocol: SetError(error_message)
    Protocol->>App: OnNetworkError(message)
    
    App->>App: 记录错误日志
    App->>Display: Alert("错误", message)
    App->>App: SetDeviceState(Idle)
    
    alt 可恢复错误
        App->>App: 启动重连机制
        App->>Network: 检查连接状态
        Network-->>App: 连接状态
        
        alt 连接正常
            App->>Protocol: 重试操作
        else 连接断开
            App->>Protocol: 重新建立连接
        end
    else 严重错误
        App->>App: SetDeviceState(FatalError)
        App->>App: 记录错误信息
        App->>App: 准备重启
    end
```

## 13. 电源管理序列

```mermaid
sequenceDiagram
    participant App as Application
    participant Board as Board
    participant Audio as AudioService
    participant Codec as AudioCodec
    participant Timer as PowerSaveTimer

    App->>Board: SetPowerSaveMode(true)
    Board->>Timer: 启动省电定时器
    
    loop 定期检查
        Timer->>Audio: 检查音频活动
        Audio-->>Timer: 最后活动时间
        
        alt 超时无活动
            Timer->>Codec: 检查输入通道
            Codec-->>Timer: 最后输入时间
            
            alt 输入超时
                Timer->>Codec: EnableInput(false)
                Codec->>Codec: 关闭ADC
            end
            
            Timer->>Codec: 检查输出通道
            Codec-->>Timer: 最后输出时间
            
            alt 输出超时
                Timer->>Codec: EnableOutput(false)
                Codec->>Codec: 关闭DAC
            end
        end
    end
    
    Note over App,Timer: 检测到活动时自动重新启用通道
```

## 14. 双网络切换序列

```mermaid
sequenceDiagram
    participant User as 用户
    participant App as Application
    participant Board as DualNetworkBoard
    participant WifiBoard as WifiBoard
    participant Ml307Board as Ml307Board
    participant Network as NetworkInterface

    User->>App: 触发网络切换
    App->>Board: SwitchNetworkType()
    
    Board->>Board: 获取当前网络类型
    Board->>Board: 切换网络类型
    
    alt 切换到WiFi
        Board->>Ml307Board: 断开连接
        Board->>WifiBoard: 初始化
        Board->>WifiBoard: StartNetwork()
        WifiBoard->>Network: Connect()
    else 切换到ML307
        Board->>WifiBoard: 断开连接
        Board->>Ml307Board: 初始化
        Board->>Ml307Board: StartNetwork()
        Ml307Board->>Network: Connect()
    end
    
    Network-->>Board: 连接成功
    Board->>Board: 保存网络类型到Settings
    Board-->>App: 切换完成
```

## 15. 序列图总结

### 15.1 关键交互模式

1. **请求-响应模式**: MCP工具调用、协议消息交换
2. **事件驱动模式**: 状态变化通知、音频事件处理
3. **流式传输模式**: 音频数据传输、OTA升级
4. **回调模式**: 异步事件处理、网络状态变化

### 15.2 同步机制

- **阻塞调用**: 关键操作等待完成
- **异步回调**: 网络操作、音频处理
- **事件通知**: 状态变化、错误处理
- **队列缓冲**: 音频数据传输

### 15.3 错误处理

- **重试机制**: 网络连接、OTA下载
- **状态恢复**: 连接断开后自动恢复
- **错误通知**: 通过回调函数通知上层
- **日志记录**: 记录关键操作和错误

### 15.4 性能优化

- **异步处理**: 避免阻塞主线程
- **批量操作**: 音频数据批量处理
- **缓存机制**: 资源文件缓存
- **延迟加载**: 按需初始化模块

这些序列图展示了系统各模块之间的详细交互过程，有助于理解系统的运行机制和调试问题。

