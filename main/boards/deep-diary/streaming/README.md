# 流媒体模块 - Streaming Module

本目录包含 ATK-DNESP32S3 开发板的流媒体功能实现。

## 功能模式

### 1. TCP 客户端模式 (tcp_client)

**文件：** `tcp_client.h`, `tcp_client.cc`

TCP 客户端模式用于将摄像头采集的 JPEG 图像帧实时发送到远程 TCP 服务器。

#### 特性
- ✅ 自动重连机制
- ✅ 双线程架构（发送/接收分离）
- ✅ 实时传输摄像头帧（约30fps）
- ✅ 断线检测和错误处理
- ✅ 可配置服务器地址和端口

#### 配置（在 `config.h` 中）

```c
// 启用 TCP 客户端模式
#define ENABLE_TCP_CLIENT_MODE 1

// TCP 服务器配置
#define TCP_SERVER_IP "192.168.31.25"
#define TCP_SERVER_PORT 8080
```

#### 使用方法

TCP 客户端会在 WiFi 连接成功后自动启动，无需手动调用。

**手动控制：**
```cpp
#include "streaming/tcp_client.h"

// 初始化（带自定义配置）
tcp_client_config_t config = {
    .server_ip = "192.168.1.100",
    .server_port = 8080,
    .auto_reconnect = true,
    .reconnect_interval = 3000
};
tcp_client_init(&config);

// 启动客户端
tcp_client_start();

// 获取连接状态
tcp_client_state_t state = tcp_client_get_state();

// 运行时修改服务器地址
tcp_client_set_server("192.168.1.200", 9090);

// 停止客户端
tcp_client_stop();
```

#### 服务器端程序

项目提供了**三个不同版本**的 Python TCP 服务器程序，位于 `scripts/` 目录：

##### 1. 简化版 (`tcp_video_server_simple.py`)
**适合：** 快速测试、本地开发
```bash
cd scripts
python tcp_video_server_simple.py
```

##### 2. 完整版 (`tcp_video_server.py`)
**适合：** 本地监控、视频录制、专业开发
```bash
# 基本使用
python tcp_video_server.py

# 启用视频录制
python tcp_video_server.py --save-video --save-dir recordings

# 完整参数
python tcp_video_server.py --host 0.0.0.0 --port 8080 --save-video --fps 30
```

**功能特性：**
- ✅ 实时显示视频流
- ✅ 自动保存视频（MP4 格式）
- ✅ 截图保存（快捷键 `s`）
- ✅ 多客户端支持
- ✅ 详细统计信息（帧率、带宽等）
- ✅ 图像信息覆盖层

##### 3. Web 版 (`tcp_video_server_web.py`)
**适合：** 云端部署、远程监控、多用户访问
```bash
# 启动服务
python tcp_video_server_web.py --tcp-port 8080 --web-port 8000

# 浏览器访问
# http://localhost:8000
```

**功能特性：**
- ✅ 浏览器访问（无需 OpenCV 窗口）
- ✅ 响应式 Web 界面
- ✅ 实时状态显示
- ✅ 在线截图下载
- ✅ 支持多用户同时观看
- ✅ 适合云服务器部署

##### 安装依赖
```bash
cd scripts
pip install -r requirements_tcp_server.txt
```

##### 快速启动脚本
```bash
cd scripts
./start_server.sh
```

**详细文档：** 查看 `scripts/TCP_SERVER_README.md` 获取完整使用指南和云端部署教程。

---

### 2. MJPEG 服务器模式 (mjpeg_server)

**文件：** `mjpeg_server.h`, `mjpeg_server.cc`

MJPEG 服务器模式用于通过 HTTP 协议提供 Motion JPEG 视频流，可以直接在浏览器中查看。

#### 配置（在 `config.h` 中）

```c
// 启用 MJPEG 服务器模式
#define ENABLE_TCP_CLIENT_MODE 0
#define ENABLE_MJPEG_FEATURE 1
```

#### 访问

WiFi 连接后，通过浏览器访问：
```
http://<ESP32-IP>:8080/stream
```

---

## 模式切换

TCP 客户端模式和 MJPEG 服务器模式**互斥**，只能同时启用一个。

在 `config.h` 中修改配置：

### 切换到 TCP 客户端模式
```c
#define ENABLE_TCP_CLIENT_MODE 1    // 启用
#define ENABLE_MJPEG_FEATURE 0      // 禁用
```

### 切换到 MJPEG 服务器模式
```c
#define ENABLE_TCP_CLIENT_MODE 0    // 禁用
#define ENABLE_MJPEG_FEATURE 1      // 启用
```

---

## 技术细节

### TCP 客户端架构

```
┌─────────────────────────────────────┐
│         TCP Client                  │
├─────────────────────────────────────┤
│                                     │
│  ┌──────────────┐  ┌─────────────┐ │
│  │  接收线程    │  │  发送线程   │ │
│  │              │  │             │ │
│  │ - 连接管理   │  │ - 获取帧    │ │
│  │ - 断线重连   │  │ - 发送数据  │ │
│  │ - 接收指令   │  │ - 频率控制  │ │
│  └──────────────┘  └─────────────┘ │
│         │                 │         │
│         └────── TCP ──────┘         │
│                Socket               │
└─────────────────────────────────────┘
```

### 数据流
```
Camera → esp_camera_fb_get() → TCP Send → Network → Server
```

### 性能参数
- 帧率：约30fps
- 分辨率：QVGA (320x240)
- 格式：JPEG
- 传输协议：TCP

---

## 调试

### 查看日志
```
ESP_LOGI("TCP_CLIENT", "连接成功！");
ESP_LOGE("TCP_CLIENT", "发送失败: errno %d", errno);
```

### 常见问题

1. **无法连接到服务器**
   - 检查服务器 IP 和端口配置
   - 确认服务器正在运行
   - 检查防火墙设置

2. **频繁断线重连**
   - 检查网络质量
   - 调整 `reconnect_interval` 参数
   - 检查服务器处理能力

3. **图像传输卡顿**
   - 降低帧率（修改发送线程延时）
   - 检查网络带宽
   - 优化 JPEG 质量参数

---

## 版本历史

- **v1.0** (2025-10-23)
  - 初始版本
  - 支持 TCP 客户端模式
  - 基于 ALIENTEK lwip_demo 示例代码
