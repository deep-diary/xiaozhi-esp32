# RTSP 流媒体迁移计划

## 当前状态
- ✅ 已禁用 MJPEG HTTP 服务器
- ✅ 相机功能保持正常（可采集图像）
- ⏳ 等待架构重构后实现 RTSP 推流

## 目标
将摄像头采集的视频流通过 RTSP 协议推送到 MediaMTX 服务器

## MediaMTX 服务器
MediaMTX 是一个现代化的实时媒体服务器，支持：
- RTSP/RTMP/HLS/WebRTC 协议
- 低延迟流媒体传输
- 多路流管理
- H.264/H.265 编码

项目地址：https://github.com/bluenviron/mediamtx

## 迁移步骤

### 1. 选择 RTSP 客户端库
**推荐方案**：使用 ESP-IDF 内置的 RTSP 组件或第三方库

选项A：使用 live555 库（C++）
- 优点：功能完整，稳定可靠
- 缺点：代码量大，内存占用较高

选项B：自实现轻量级 RTSP 推流
- 优点：内存占用小，可定制
- 缺点：需要实现 RTSP 协议和 RTP 打包

**建议**：先使用轻量级自实现方案，验证可行性

### 2. 视频编码方案
**当前**：
- 摄像头输出：RGB565 或 JPEG
- MJPEG：直接发送 JPEG 帧

**RTSP 方案**：
```
摄像头 → JPEG编码 → MJPEG over RTP → RTSP
  或
摄像头 → H.264编码 → H.264 over RTP → RTSP
```

**推荐**：
- 第一阶段：MJPEG over RTP（简单，延迟低）
- 第二阶段：H.264 编码（带宽低，需要硬件编码器或软件编码）

### 3. 实现架构

#### 3.1 创建 RTSP 推流模块
```
streaming/
├── rtsp_client.h        # RTSP客户端接口
├── rtsp_client.cc       # RTSP协议实现
├── rtp_mjpeg.h          # RTP MJPEG打包
├── rtp_mjpeg.cc         # RTP实现
└── README_RTSP.md       # RTSP使用文档
```

#### 3.2 核心类设计
```cpp
class RtspPusher {
public:
    RtspPusher(const std::string& server_url);
    ~RtspPusher();
    
    bool Connect();                    // 连接到MediaMTX
    bool PushFrame(camera_fb_t* fb);   // 推送JPEG帧
    void Disconnect();
    
private:
    std::string rtsp_url_;
    int socket_fd_;
    uint32_t timestamp_;
    uint16_t seq_number_;
};
```

#### 3.3 集成到 BoardExtensions
```cpp
// board_extensions.h
class RtspPusher;

class BoardExtensions {
    // ...
    void InitializeRtspPusher(const std::string& server_url);
    void StartRtspStream();
    void StopRtspStream();
    
private:
    std::unique_ptr<RtspPusher> rtsp_pusher_;
    TaskHandle_t rtsp_task_handle_;
};
```

### 4. MediaMTX 服务器配置

#### 4.1 安装 MediaMTX
```bash
# 下载最新版本
wget https://github.com/bluenviron/mediamtx/releases/download/vX.X.X/mediamtx_vX.X.X_linux_amd64.tar.gz
tar -xzf mediamtx_vX.X.X_linux_amd64.tar.gz
cd mediamtx
```

#### 4.2 配置文件 (mediamtx.yml)
```yaml
rtspAddress: :8554
rtmpAddress: :1935
hlsAddress: :8888
webrtcAddress: :8889

paths:
  esp32cam:
    source: publisher
    sourceProtocol: rtsp
    runOnReady: echo "ESP32 camera connected"
    runOnNotReady: echo "ESP32 camera disconnected"
```

#### 4.3 启动服务器
```bash
./mediamtx
```

### 5. ESP32 推流配置
```cpp
// config.h
#define RTSP_SERVER_URL "rtsp://192.168.1.100:8554/esp32cam"
#define RTSP_FRAME_RATE 10
#define RTSP_JPEG_QUALITY 80

// board_extensions.cc
void BoardExtensions::InitializeRtspPusher(const std::string& server_url) {
    if (!camera_) {
        ESP_LOGE(TAG, "相机未初始化");
        return;
    }
    
    rtsp_pusher_ = std::make_unique<RtspPusher>(server_url);
    ESP_LOGI(TAG, "RTSP推流器创建完成");
}

void BoardExtensions::StartRtspStream() {
    if (!rtsp_pusher_->Connect()) {
        ESP_LOGE(TAG, "连接RTSP服务器失败");
        return;
    }
    
    // 创建推流任务
    xTaskCreate(
        rtsp_stream_task,
        "rtsp_stream",
        8192,
        this,
        5,
        &rtsp_task_handle_
    );
}

static void rtsp_stream_task(void* arg) {
    auto* ext = static_cast<BoardExtensions*>(arg);
    
    while (1) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) {
            ext->rtsp_pusher_->PushFrame(fb);
            esp_camera_fb_return(fb);
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // 10fps
    }
}
```

### 6. 播放测试

#### 6.1 使用 VLC 播放
```bash
vlc rtsp://192.168.1.100:8554/esp32cam
```

#### 6.2 使用 FFplay 播放
```bash
ffplay rtsp://192.168.1.100:8554/esp32cam
```

#### 6.3 Web 播放（通过 MediaMTX 的 HLS）
```
http://192.168.1.100:8888/esp32cam
```

## 预期优势

### vs MJPEG HTTP
| 特性 | MJPEG HTTP | RTSP |
|------|-----------|------|
| 延迟 | 中等 (200-500ms) | 低 (100-200ms) |
| 带宽效率 | 较低 | 高（可选H.264） |
| 标准化 | HTTP | RTSP（标准协议） |
| 播放器支持 | 浏览器 | VLC, FFmpeg, 专业播放器 |
| 多客户端 | 需自行管理 | MediaMTX自动管理 |

## 时间计划
1. **阶段1**：完成轻量级 RTSP 客户端实现（1-2天）
2. **阶段2**：集成到 BoardExtensions（0.5天）
3. **阶段3**：测试和优化（1天）
4. **阶段4**：文档编写（0.5天）

**总计**：约3-4天

## 参考资源
- RTSP RFC 2326: https://www.rfc-editor.org/rfc/rfc2326
- RTP RFC 3550: https://www.rfc-editor.org/rfc/rfc3550
- MJPEG over RTP RFC 2435: https://www.rfc-editor.org/rfc/rfc2435
- MediaMTX 文档: https://github.com/bluenviron/mediamtx
- ESP32 Camera 示例: https://github.com/espressif/esp32-camera

## 备注
当前已禁用的功能将在实现 RTSP 推流后恢复，届时用户可以：
1. 通过 MediaMTX 访问视频流
2. 使用多种播放器观看
3. 实现低延迟的实时监控
4. 支持多客户端同时观看

