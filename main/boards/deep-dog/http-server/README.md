# DeepDog HTTP 服务（控制页 + MJPEG）

本目录提供 **局域网 HTTP 服务**：内嵌网页遥控 `DogControl`，以及 **MJPEG 实时画面**（`multipart/x-mixed-replace`）。相机走板级 **`EspVideo`**（OV3660 DVP），JPEG 使用工程内 **`image_to_jpeg`**。

## 文件

| 文件 | 说明 |
|------|------|
| `deep_dog_http_server.h` | `DeepDogHttpServer`、`DeepDogCaptureMode` 声明 |
| `deep_dog_http_server.cc` | `esp_http_server`、采集 worker、`dog_web_cmd` 任务 |

板级在 `esp_sparkbot_board.cc` 的 `InitializeCamera()` 末尾创建并 `Start()`。构建由顶层 `CMakeLists.txt` 递归收集本目录 `.cc`，无需单独 CMake。

## 配置（`boards/deep-dog/config.h`）

- **`DEEP_DOG_HTTP_SERVER_ENABLE`**：置 `0` 关闭整个模块（桩实现，`Start()` 恒为 false）。
- **`DEEP_DOG_HTTP_SERVER_PORT`**：监听端口，默认 **8080**。

## 摄像头采集三态

| 模式 | 含义 |
|------|------|
| **Off** | 不采帧，worker 低占空休眠。 |
| **PeriodicSample** | 约 **1Hz** `CaptureOnly()`，日志占位，便于后续接人脸/检测；不刷 LCD。 |
| **Streaming** | 约 **8fps**（代码内 `stream_target_fps_`）采帧 → JPEG → 共享缓冲；`/stream` 只读该缓冲推送。 |

默认上电为 **Off**，需在网页切到 **视频流** 再观看 MJPEG，以省 CPU/带宽。

## HTTP 接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/` | 控制页（模式 + 动作按钮 + `<img src="/stream">`） |
| GET | `/stream` | MJPEG |
| GET | `/api/status` | JSON：`mode`、`stream_clients`、`has_jpeg`、`port` |
| POST | `/api/capture_mode?mode=off` 或 `periodic` 或 `stream` | 切换采集模式 |
| POST | `/api/cmd?cmd=...` | 投递狗指令，见下表 |
| GET | `/api/face` | 人脸框 + `local_id` / `display_name` |
| POST | `/api/face_enable?enabled=0\|1` | 开关人脸检测/识别 |
| POST | `/api/immich_config?api_key=...&api_url=...` | Immich Key 写入 NVS（S05） |
| GET | `/api/immich_status` | Immich 配置/上次结果（无 Key 明文） |
| POST | `/api/face_refresh_name?local_id=` | 强制下次再取 Immich 真名 |

**`cmd` 取值**：`init`、`forward`、`back`、`stand`、`liedown`、`dance`、`stop_walk`。  
指令进 **FreeRTOS 队列**，由 **`dog_web_cmd`** 任务调用 `DogControl`，避免在 httpd 回调里长时间阻塞（CAN/状态机安全）。

## 使用

1. 设备连上 Wi‑Fi。串口在 **`IP_EVENT_STA_GOT_IP`** 后会打印 **`http://<IP>:<端口>/`** 与 **`/stream`** 完整 URL（`Start()` 若早于拿 IP，会先打一条「IP 尚未就绪」提示）。
2. 浏览器访问 **`http://<IP>:<端口>/`**。
3. 需要画面时点击 **「视频流」**；不用时切回 **「关闭」**。

## 注意

- **`/stream` 与 API 并发**：MJPEG 在独立任务中跑（`httpd_req_async`），不占用 `httpd` 主线程，拉流时 **`/api/cmd`、模式切换仍可响应**。并发拉流路数由队列深度限制（默认 2），满则返回 503。
- **屏幕无显示**：若日志里是 **`bread-compact-wifi`** 且 **`SSD1306` / `i2c transaction failed`**，说明当前固件是面包板 OLED 板型而非 **deep-dog**，或 I2C 接线/地址与配置不符。要用 DeepDog 的 SPI 屏与 HTTP 功能，请在 **`menuconfig` / `sdkconfig` 中选 deep-dog 板** 并重新编译烧录，硬件需与板型一致。
- **与 MCP 拍照、触摸 Explain 共用同一 `EspVideo`**，无板级互斥时可能互抢；高负载下建议文档约定优先级或后续加锁。
- 参考实现见 `boards/deep-diary/streaming/mjpeg_server`（基于 `esp_camera`）；本实现针对 **EspVideo + V4L2 帧格式**，RGB565 行 stride 在编码前会压成紧密缓冲。
- 更完整的产品化项（鉴权、限连接数、FPS/画质可配）与需求切片见 [../swrs/ROADMAP.md](../swrs/ROADMAP.md)（V-S01/S02 等）。
