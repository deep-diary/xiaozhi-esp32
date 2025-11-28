# 数据流图

本文档详细描述小智 ESP32 项目中各种数据的流动路径和处理过程。

## 1. 音频数据流

### 1.1 音频输入流（上行：设备 → 服务器）

```mermaid
graph TD
    A[麦克风硬件] -->|I2S PCM数据| B[AudioCodec]
    B -->|原始PCM<br/>采样率: 可变<br/>声道: 1/2| C[AudioInputTask]
    
    C -->|16kHz PCM| D{唤醒词检测?}
    D -->|是| E[WakeWord引擎]
    D -->|否| F[AudioProcessor]
    
    E -->|检测到唤醒词| G[Application::WakeWordInvoke]
    E -->|继续检测| C
    
    F -->|处理后PCM<br/>AEC/VAD处理| H[audio_encode_queue]
    H -->|PCM数据| I[OpusCodecTask]
    I -->|Opus编码<br/>16kHz, 60ms帧| J[audio_send_queue]
    J -->|AudioStreamPacket| K[Application]
    K -->|PopPacketFromSendQueue| L[Protocol]
    
    L -->|WebSocket二进制帧<br/>或UDP加密包| M[网络传输]
    M -->|Opus音频数据| N[服务器]
    
    style A fill:#e1f5ff
    style N fill:#fff4e1
    style I fill:#ffe1f5
    style F fill:#e1ffe1
```

**详细说明：**

1. **硬件采集阶段**
   - 麦克风通过 I2S 接口将模拟信号转换为数字 PCM 数据
   - `AudioCodec` 负责 I2S 配置和数据读取
   - 采样率可能是 16kHz、24kHz、48kHz 等（取决于硬件）

2. **重采样阶段**（如果需要）
   - 如果硬件采样率不是 16kHz，`AudioService` 使用 `input_resampler_` 进行重采样
   - 双声道输入会被分离为麦克风通道和参考通道（用于 AEC）

3. **唤醒词检测阶段**
   - `AudioInputTask` 持续读取音频数据
   - 如果启用唤醒词检测，数据会同时送入 `WakeWord` 引擎
   - 检测到唤醒词后触发 `Application::WakeWordInvoke()`

4. **音频处理阶段**
   - 数据送入 `AudioProcessor` 进行处理：
     - **AEC (回声消除)**：如果启用设备端 AEC，会使用参考通道消除回声
     - **VAD (语音活动检测)**：检测是否有语音输入
     - **降噪**：可选的处理步骤
   - 处理后的数据推入 `audio_encode_queue`

5. **编码阶段**
   - `OpusCodecTask` 从队列取出 PCM 数据
   - 使用 Opus 编码器编码为 Opus 格式
   - 编码参数：16kHz 采样率，单声道，60ms 帧时长
   - 编码后的数据推入 `audio_send_queue`

6. **传输阶段**
   - `Application` 从队列取出 `AudioStreamPacket`
   - 根据协议类型选择传输方式：
     - **WebSocket**：直接作为二进制帧发送
     - **MQTT+UDP**：使用 AES-CTR 加密后通过 UDP 发送
   - 数据包可能包含时间戳（用于服务器端 AEC）

### 1.2 音频输出流（下行：服务器 → 设备）

```mermaid
graph TD
    A[服务器] -->|Opus音频数据| B[网络传输]
    B -->|WebSocket二进制帧<br/>或UDP加密包| C[Protocol]
    
    C -->|OnIncomingAudio回调| D[Application]
    D -->|PushPacketToDecodeQueue| E[audio_decode_queue]
    E -->|Opus数据包| F[OpusCodecTask]
    
    F -->|Opus解码<br/>采样率: 24kHz| G[audio_playback_queue]
    G -->|PCM数据| H[AudioOutputTask]
    
    H -->|PCM数据<br/>可能需要重采样| I[AudioCodec]
    I -->|I2S输出| J[扬声器硬件]
    
    style A fill:#fff4e1
    style J fill:#e1f5ff
    style F fill:#ffe1f5
    style H fill:#e1ffe1
```

**详细说明：**

1. **接收阶段**
   - `Protocol` 接收来自服务器的音频数据
   - WebSocket：二进制帧直接作为 Opus 数据
   - MQTT+UDP：UDP 数据包需要先解密

2. **解码阶段**
   - `Application` 将接收到的数据包推入 `audio_decode_queue`
   - `OpusCodecTask` 从队列取出 Opus 数据包
   - 使用 Opus 解码器解码为 PCM 数据
   - 服务器端通常使用 24kHz 采样率（更好的音质）

3. **重采样阶段**（如果需要）
   - 如果硬件输出采样率与解码后的采样率不一致，需要进行重采样
   - 例如：24kHz → 48kHz

4. **播放阶段**
   - `AudioOutputTask` 从 `audio_playback_queue` 取出 PCM 数据
   - 通过 `AudioCodec` 的 `OutputData()` 方法输出
   - `AudioCodec` 通过 I2S 接口将数据发送到扬声器

### 1.3 音频处理详细流程

```mermaid
graph LR
    A[原始PCM] --> B{双声道?}
    B -->|是| C[分离麦克风/参考通道]
    B -->|否| D[单声道处理]
    
    C --> E[重采样到16kHz]
    D --> E
    
    E --> F{AEC模式?}
    F -->|设备端AEC| G[AfeAudioProcessor<br/>使用参考通道]
    F -->|服务器端AEC| H[NoAudioProcessor<br/>保留双声道]
    F -->|无AEC| I[NoAudioProcessor<br/>单声道]
    
    G --> J[VAD检测]
    H --> J
    I --> J
    
    J -->|有语音| K[处理后PCM]
    J -->|无语音| L[丢弃]
    
    K --> M[Opus编码]
    
    style G fill:#ffe1f5
    style H fill:#ffe1f5
    style I fill:#ffe1f5
    style J fill:#e1ffe1
```

## 2. 控制消息流

### 2.1 设备 → 服务器消息流

```mermaid
graph TD
    A[用户操作/系统事件] --> B[Application]
    B --> C{消息类型}
    
    C -->|开始监听| D[Protocol::SendStartListening]
    C -->|停止监听| E[Protocol::SendStopListening]
    C -->|中断说话| F[Protocol::SendAbortSpeaking]
    C -->|MCP消息| G[Protocol::SendMcpMessage]
    C -->|唤醒词检测| H[Protocol::SendWakeWordDetected]
    
    D --> I[构建JSON消息]
    E --> I
    F --> I
    G --> I
    H --> I
    
    I --> J{协议类型}
    J -->|WebSocket| K[WebSocket::SendText]
    J -->|MQTT| L[MqttClient::Publish]
    
    K --> M[网络传输]
    L --> M
    M --> N[服务器]
    
    style A fill:#e1f5ff
    style N fill:#fff4e1
    style I fill:#ffe1f5
```

**消息类型示例：**

1. **开始监听**
   ```json
   {
     "session_id": "xxx",
     "type": "listen",
     "state": "start",
     "mode": "auto" | "manual" | "realtime"
   }
   ```

2. **MCP 消息**
   ```json
   {
     "session_id": "xxx",
     "type": "mcp",
     "payload": {
       "jsonrpc": "2.0",
       "id": 1,
       "result": {...}
     }
   }
   ```

### 2.2 服务器 → 设备消息流

```mermaid
graph TD
    A[服务器] -->|JSON消息| B[网络传输]
    B --> C{协议类型}
    
    C -->|WebSocket| D[WebSocket::OnData<br/>text frame]
    C -->|MQTT| E[MqttClient::OnMessage]
    
    D --> F[Protocol::OnIncomingJson]
    E --> F
    
    F --> G[Application::HandleJsonMessage]
    G --> H{消息类型}
    
    H -->|STT| I[显示识别文本]
    H -->|TTS start| J[开始播放音频]
    H -->|TTS stop| K[停止播放音频]
    H -->|LLM| L[更新表情/UI]
    H -->|MCP| M[McpServer::ParseMessage]
    H -->|System| N[系统命令处理]
    
    M --> O{方法类型}
    O -->|tools/list| P[返回工具列表]
    O -->|tools/call| Q[执行工具调用]
    O -->|initialize| R[初始化响应]
    
    P --> S[Protocol::SendMcpMessage]
    Q --> S
    R --> S
    
    style A fill:#fff4e1
    style I fill:#e1ffe1
    style J fill:#e1ffe1
    style M fill:#ffe1f5
```

**消息类型示例：**

1. **STT (语音识别)**
   ```json
   {
     "session_id": "xxx",
     "type": "stt",
     "text": "用户说的话"
   }
   ```

2. **TTS (语音合成)**
   ```json
   {
     "session_id": "xxx",
     "type": "tts",
     "state": "start" | "stop" | "sentence_start",
     "text": "要播放的文本"
   }
   ```

3. **MCP 工具调用**
   ```json
   {
     "session_id": "xxx",
     "type": "mcp",
     "payload": {
       "jsonrpc": "2.0",
       "method": "tools/call",
       "params": {
         "name": "self.audio_speaker.set_volume",
         "arguments": {"volume": 50}
       },
       "id": 1
     }
   }
   ```

## 3. MCP 工具调用流程

```mermaid
sequenceDiagram
    participant Server as 服务器
    participant Protocol as Protocol
    participant App as Application
    participant MCP as McpServer
    participant Tool as McpTool
    participant Board as Board

    Server->>Protocol: MCP tools/call 请求
    Protocol->>App: OnIncomingJson
    App->>MCP: ParseMessage
    
    MCP->>MCP: 解析 JSON-RPC
    MCP->>Tool: 查找工具
    
    alt 工具存在
        MCP->>Tool: Call(PropertyList)
        Tool->>Board: 调用硬件接口
        Board-->>Tool: 返回结果
        Tool-->>MCP: 返回结果JSON
        MCP->>App: SendMcpMessage(结果)
        App->>Protocol: SendMcpMessage
        Protocol->>Server: MCP result 响应
    else 工具不存在
        MCP->>App: SendMcpMessage(错误)
        App->>Protocol: SendMcpMessage
        Protocol->>Server: MCP error 响应
    end
```

## 4. 设备状态流转数据流

```mermaid
graph TD
    A[系统启动] -->|初始化| B[Application::Start]
    B --> C[设备状态: Starting]
    
    C --> D[初始化硬件]
    D --> E[初始化音频服务]
    E --> F[初始化网络]
    
    F --> G{网络就绪?}
    G -->|否| H[设备状态: WifiConfiguring]
    G -->|是| I[设备状态: Activating]
    
    H --> J[WiFi配置]
    J --> I
    
    I --> K[检查新版本]
    K --> L{需要激活?}
    L -->|是| M[显示激活码]
    L -->|否| N[设备状态: Idle]
    
    M --> O[等待激活]
    O --> N
    
    N --> P{用户操作}
    P -->|唤醒词/按键| Q[设备状态: Connecting]
    P -->|手动监听| Q
    
    Q --> R[打开音频通道]
    R --> S{连接成功?}
    S -->|否| N
    S -->|是| T[设备状态: Listening]
    
    T --> U[发送音频数据]
    U --> V{收到TTS?}
    V -->|是| W[设备状态: Speaking]
    V -->|否| T
    
    W --> X[播放音频]
    X --> Y{TTS结束?}
    Y -->|是| Z{自动模式?}
    Y -->|否| W
    
    Z -->|是| T
    Z -->|否| N
    
    style C fill:#e1f5ff
    style N fill:#e1ffe1
    style T fill:#fff4e1
    style W fill:#ffe1f5
```

## 5. 网络连接数据流

### 5.1 WebSocket 连接流程

```mermaid
sequenceDiagram
    participant App as Application
    participant Protocol as WebsocketProtocol
    participant WS as WebSocket
    participant Server as 服务器

    App->>Protocol: OpenAudioChannel()
    Protocol->>WS: Connect(url, headers)
    WS->>Server: WebSocket握手
    
    Server-->>WS: 连接成功
    WS-->>Protocol: OnConnected
    
    Protocol->>Server: Hello消息 (JSON)
    Server-->>Protocol: Hello响应 (JSON)
    
    Protocol->>Protocol: 解析session_id
    Protocol-->>App: OnAudioChannelOpened
    
    Note over App,Server: 音频通道已打开
    
    loop 音频传输
        App->>Protocol: SendAudio(packet)
        Protocol->>WS: SendBinary(opus_data)
        WS->>Server: 二进制帧
        
        Server->>WS: 二进制帧 (TTS音频)
        WS-->>Protocol: OnData(binary=true)
        Protocol-->>App: OnIncomingAudio(packet)
    end
    
    loop 控制消息
        App->>Protocol: SendText(json)
        Protocol->>WS: SendText(json)
        WS->>Server: 文本帧
        
        Server->>WS: 文本帧 (JSON消息)
        WS-->>Protocol: OnData(binary=false)
        Protocol-->>App: OnIncomingJson(json)
    end
```

### 5.2 MQTT + UDP 连接流程

```mermaid
sequenceDiagram
    participant App as Application
    participant Protocol as MqttProtocol
    participant MQTT as MqttClient
    participant UDP as UdpClient
    participant Server as 服务器

    App->>Protocol: OpenAudioChannel()
    Protocol->>MQTT: Connect(broker, client_id, ...)
    MQTT->>Server: MQTT连接
    
    Server-->>MQTT: 连接成功
    MQTT-->>Protocol: OnConnected
    
    Protocol->>Server: Hello消息 (MQTT)
    Server-->>Protocol: Hello响应 (MQTT)<br/>包含UDP信息
    
    Protocol->>Protocol: 解析UDP配置<br/>(server, port, key, nonce)
    Protocol->>UDP: Connect(server, port)
    Protocol->>UDP: 初始化AES加密
    
    UDP->>Server: UDP连接
    Server-->>UDP: 连接成功
    UDP-->>Protocol: OnConnected
    
    Protocol-->>App: OnAudioChannelOpened
    
    Note over App,Server: 音频通道已打开
    
    loop 音频传输 (UDP)
        App->>Protocol: SendAudio(packet)
        Protocol->>Protocol: AES-CTR加密
        Protocol->>UDP: Send(encrypted_data)
        UDP->>Server: UDP数据包
        
        Server->>UDP: UDP数据包 (加密TTS音频)
        UDP-->>Protocol: OnData(encrypted_data)
        Protocol->>Protocol: AES-CTR解密
        Protocol-->>App: OnIncomingAudio(packet)
    end
    
    loop 控制消息 (MQTT)
        App->>Protocol: SendText(json)
        Protocol->>MQTT: Publish(topic, json)
        MQTT->>Server: MQTT消息
        
        Server->>MQTT: MQTT消息 (JSON)
        MQTT-->>Protocol: OnMessage(topic, json)
        Protocol-->>App: OnIncomingJson(json)
    end
```

## 6. 唤醒词检测数据流

```mermaid
graph TD
    A[AudioInputTask] -->|16kHz PCM| B[WakeWord引擎]
    B --> C{检测到唤醒词?}
    
    C -->|否| A
    C -->|是| D[触发回调]
    
    D --> E[Application::WakeWordInvoke]
    E --> F{设备状态?}
    
    F -->|Idle| G[打开音频通道]
    F -->|Speaking| H[中断当前TTS]
    
    G --> I[开始监听]
    H --> I
    
    I --> J[发送唤醒词音频<br/>到服务器]
    J --> K[发送listen消息<br/>state: detect]
    
    style B fill:#ffe1f5
    style E fill:#e1ffe1
    style I fill:#fff4e1
```

## 7. OTA 升级数据流

```mermaid
graph TD
    A[Application::Start] --> B[CheckNewVersion]
    B --> C[Ota::CheckVersion]
    C --> D{有新版本?}
    
    D -->|否| E[继续正常流程]
    D -->|是| F[UpgradeFirmware]
    
    F --> G[下载固件]
    G --> H{下载成功?}
    
    H -->|否| E
    H -->|是| I[验证固件]
    I --> J{验证通过?}
    
    J -->|否| E
    J -->|是| K[写入OTA分区]
    K --> L[设置启动分区]
    L --> M[重启设备]
    
    style F fill:#ffe1f5
    style M fill:#fff4e1
```

## 8. 资源文件加载数据流

```mermaid
graph TD
    A[Application::Start] --> B[CheckAssetsVersion]
    B --> C{有下载URL?}
    
    C -->|否| D[加载本地资源]
    C -->|是| E[下载资源文件]
    
    E --> F[显示下载进度]
    F --> G{下载成功?}
    
    G -->|否| H[显示错误]
    G -->|是| I[写入SPIFFS分区]
    
    I --> D
    D --> J[Assets::Apply]
    J --> K[加载语言文件]
    J --> L[加载音频文件]
    J --> M[加载字体文件]
    J --> N[加载表情图片]
    
    K --> O[Settings存储]
    L --> O
    M --> O
    N --> O
    
    style E fill:#ffe1f5
    style J fill:#e1ffe1
```

## 9. 数据流总结

### 9.1 关键数据队列

1. **audio_encode_queue**: PCM → Opus 编码队列
2. **audio_send_queue**: 编码后的音频包队列
3. **audio_decode_queue**: 接收到的 Opus 包队列
4. **audio_playback_queue**: 解码后的 PCM 队列

### 9.2 数据格式转换

- **硬件 → 处理**: I2S PCM (可变采样率) → 16kHz PCM
- **处理 → 编码**: 16kHz PCM → Opus (16kHz, 60ms帧)
- **传输**: Opus 二进制数据（可能加密）
- **解码**: Opus → 24kHz PCM
- **播放**: 24kHz PCM → 硬件采样率 PCM → I2S

### 9.3 数据同步机制

- **EventGroup**: 任务间事件通知
- **Mutex**: 保护共享队列
- **Condition Variable**: 队列等待/通知
- **回调函数**: 异步事件处理

### 9.4 性能考虑

- **队列大小限制**: 防止内存溢出
- **及时处理**: 避免队列堆积
- **优先级调度**: 音频任务高优先级
- **缓冲管理**: 平衡延迟和稳定性

