# RTSP视频流推送使用指南

## 功能概述

本项目已集成RTSP视频流推送功能，可以将ESP32-CAM采集的视频实时推送到流媒体服务器（如MediaMTX）或直接通过VLC等播放器观看。

## 系统架构

```
ESP32-CAM (atk-dnesp32s3) <---> MediaMTX服务器 (35.192.64.247) <---> 观看端
    ↓                              ↓                                   ↓
1. 采集视频图像              2. 接收并转发RTSP流                  3. 播放视频
2. JPEG编码                  3. 支持多客户端                       
3. RTSP/RTP打包
4. 推送到网络
```

## 快速开始

### 1. 编译和烧录

确保在 `config.h` 中启用了相机功能：

```c
#define ENABLE_CAMERA_FEATURE 1    // 启用相机功能
#define ENABLE_CAN_FEATURE 0       // 禁用CAN总线（GPIO冲突）
#define ENABLE_LED_STRIP_FEATURE 0 // 禁用灯带（GPIO冲突）
```

然后编译并烧录项目：

```bash
idf.py build flash monitor
```

### 2. 连接WiFi

设备启动后，通过配网流程连接到WiFi网络。确保ESP32和MediaMTX服务器在同一网络或可以相互访问。

### 3. 启动RTSP服务

#### 方式一：通过MCP工具启动（推荐）

如果您的客户端支持MCP协议，可以使用以下MCP工具：

**启动RTSP服务：**
```json
{
  "tool": "rtsp_start",
  "arguments": {
    "fps": 10
  }
}
```

**查询RTSP状态：**
```json
{
  "tool": "rtsp_status",
  "arguments": {}
}
```

**设置帧率：**
```json
{
  "tool": "rtsp_set_fps",
  "arguments": {
    "fps": 15
  }
}
```

**停止RTSP服务：**
```json
{
  "tool": "rtsp_stop",
  "arguments": {}
}
```

#### 方式二：代码中自动启动

在 `atk_dnesp32s3.cc` 的初始化函数中添加：

```cpp
void InitializeRtspControl() {
    // ... 现有代码 ...
    
    // 自动启动RTSP服务
    rtsp_stream_->Start();
}
```

## MediaMTX服务器配置

### 1. 安装MediaMTX

在服务器 35.192.64.247 上安装MediaMTX：

```bash
# 下载MediaMTX
wget https://github.com/bluenviron/mediamtx/releases/download/v1.3.0/mediamtx_v1.3.0_linux_amd64.tar.gz
tar -xzf mediamtx_v1.3.0_linux_amd64.tar.gz

# 运行MediaMTX
./mediamtx
```

### 2. 配置MediaMTX拉流

编辑 `mediamtx.yml` 配置文件：

```yaml
paths:
  esp32cam:
    # 从ESP32拉取RTSP流
    source: rtsp://ESP32_IP_ADDRESS:554/stream
    sourceProtocol: tcp
    sourceOnDemand: yes
    runOnReady: echo "ESP32 camera connected"
```

替换 `ESP32_IP_ADDRESS` 为您的ESP32实际IP地址（可以从串口日志或MCP状态查询获取）。

### 3. 重启MediaMTX

```bash
./mediamtx mediamtx.yml
```

## 观看视频流

### 使用VLC播放器

1. 打开VLC
2. 媒体 -> 打开网络串流
3. 输入URL：
   - **直接从ESP32观看**：`rtsp://ESP32_IP:554/stream`
   - **通过MediaMTX观看**：`rtsp://35.192.64.247:8554/esp32cam`

### 使用ffplay

```bash
# 直接从ESP32观看
ffplay rtsp://ESP32_IP:554/stream

# 通过MediaMTX观看
ffplay rtsp://35.192.64.247:8554/esp32cam
```

### 使用网页播放器

可以使用基于WebRTC的播放器通过MediaMTX观看（MediaMTX支持RTSP到WebRTC的转换）：

```
http://35.192.64.247:8889/esp32cam
```

## 技术参数

| 参数 | 默认值 | 可调范围 | 说明 |
|------|--------|----------|------|
| 分辨率 | 320x240 (QVGA) | - | 由相机配置决定 |
| 帧率 | 10 fps | 1-30 fps | 可通过MCP工具调整 |
| JPEG质量 | 80 | 1-100 | 数值越高质量越好 |
| RTSP端口 | 554 | 1-65535 | 标准RTSP端口 |
| 传输协议 | RTP over UDP | - | 支持单播 |
| 视频编码 | JPEG (Motion JPEG) | - | RFC 2435标准 |

## 性能优化建议

### 1. 帧率设置

- **低帧率（5-10 fps）**：适合监控场景，网络带宽需求低
- **中帧率（10-15 fps）**：平衡流畅度和带宽
- **高帧率（15-30 fps）**：更流畅，但需要更多带宽和CPU资源

### 2. JPEG质量

- **低质量（50-70）**：文件小，带宽需求低，画质一般
- **中质量（70-85）**：平衡质量和大小（推荐）
- **高质量（85-100）**：画质好，文件大，带宽需求高

### 3. 网络优化

- 确保WiFi信号强度良好
- 使用5GHz WiFi频段（如果支持）
- 减少网络跳数，ESP32和服务器在同一局域网最佳

## 故障排查

### 问题1：无法连接到RTSP流

**可能原因：**
- ESP32未正确连接WiFi
- 防火墙阻止了554端口
- IP地址错误

**解决方案：**
```bash
# 检查ESP32 IP地址（从串口日志）
# 测试网络连通性
ping ESP32_IP

# 检查端口是否开放
nc -zv ESP32_IP 554
```

### 问题2：视频卡顿或延迟高

**可能原因：**
- 帧率设置过高
- WiFi信号弱
- 网络带宽不足

**解决方案：**
- 降低帧率到5-10 fps
- 降低JPEG质量到60-70
- 移近WiFi路由器
- 检查网络带宽使用情况

### 问题3：编译错误

**可能原因：**
- 缺少依赖
- 配置冲突

**解决方案：**
```bash
# 清理构建
idf.py fullclean

# 重新配置
idf.py menuconfig

# 确保选择正确的开发板：atk-dnesp32s3

# 重新编译
idf.py build
```

## API参考

### RtspStream类

```cpp
// 创建RTSP流对象
auto stream = std::make_unique<RtspStream>(camera, "stream", 554);

// 设置参数
stream->SetFrameRate(10);      // 设置帧率
stream->SetJpegQuality(80);     // 设置JPEG质量

// 启动服务
stream->Start();

// 获取状态
bool running = stream->IsRunning();
int clients = stream->GetClientCount();
std::string url = stream->GetRtspUrl();

// 停止服务
stream->Stop();
```

### MCP工具列表

| 工具名 | 功能 | 参数 |
|--------|------|------|
| `rtsp_start` | 启动RTSP服务 | `fps` (可选): 帧率 |
| `rtsp_stop` | 停止RTSP服务 | 无 |
| `rtsp_status` | 查询状态 | 无 |
| `rtsp_set_fps` | 设置帧率 | `fps` (必需): 1-30 |

## 常见使用场景

### 场景1：智能监控

```cpp
// 启动低帧率监控流
rtsp_stream_->SetFrameRate(5);
rtsp_stream_->SetJpegQuality(70);
rtsp_stream_->Start();
```

### 场景2：实时视频通话

```cpp
// 启动较高帧率流
rtsp_stream_->SetFrameRate(20);
rtsp_stream_->SetJpegQuality(85);
rtsp_stream_->Start();
```

### 场景3：录像存储

使用MediaMTX的录制功能：

```yaml
paths:
  esp32cam:
    source: rtsp://ESP32_IP:554/stream
    record: yes
    recordPath: /path/to/recordings/%Y-%m-%d_%H-%M-%S.mp4
```

## 技术细节

### RTSP协议支持

实现遵循以下RFC标准：
- **RFC 2326**: RTSP 1.0协议
- **RFC 2435**: RTP JPEG格式
- **RFC 3550**: RTP协议

### 支持的RTSP方法

- `OPTIONS`: 查询服务器支持的方法
- `DESCRIBE`: 获取媒体描述（SDP）
- `SETUP`: 建立传输会话
- `PLAY`: 开始流媒体传输
- `TEARDOWN`: 关闭会话

### RTP打包

- Payload Type: 26 (JPEG)
- 时钟频率: 90000 Hz
- 传输方式: UDP单播
- MTU: 1400字节（避免分片）

## 许可证

本实现基于ESP-IDF开源项目，遵循Apache 2.0许可证。

## 支持与反馈

如有问题或建议，请提交Issue或联系开发团队。

---

**版本**: 1.0.0  
**最后更新**: 2025-10-19

