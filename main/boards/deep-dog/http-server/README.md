# DeepDog HTTP 服务（控制页 + 可选 MJPEG）

本目录提供 **局域网 HTTP 服务**：内嵌网页遥控 `DogControl`，以及可选 **MJPEG**（`multipart/x-mixed-replace`）。  
**采帧 / 人脸 / MediaMTX 推流** 由 [`../vision/`](../vision/) 的 `VisionFrameHub` 统一调度；本模块不再拥有相机 worker，也不启动/停止 `face_ai`。

## 文件

| 文件 | 说明 |
|------|------|
| `deep_dog_http_server.h` | `DeepDogHttpServer`、`DeepDogCaptureMode` 声明 |
| `deep_dog_http_server.cc` | `esp_http_server`、MJPEG 发送、`dog_web_cmd` 任务 |

板级在 `StartNetwork()`：先 `DeepDogFaceAiRuntimeStart` + `VisionFrameHub::Start`，再 `http_server_->Start()`。

## 配置（`boards/deep-dog/config.h`）

- **`DEEP_DOG_HTTP_SERVER_ENABLE`**：置 `0` 关闭整个模块（桩实现，`Start()` 恒为 false）。
- **`DEEP_DOG_HTTP_SERVER_PORT`**：监听端口，默认 **8080**。
- 推流相关：见 [`../vision/vision_config.h`](../vision/vision_config.h)。

## 视频发布模式（与 VisionHub 对齐）

| 模式 API | 含义 |
|------|------|
| **off** / **periodic** | 不发布视频；人脸仍可由 Hub 静默采帧（默认人脸开） |
| **stream** / **mjpeg** | 局域网 HTTP MJPEG（`/stream`） |
| **rtsp_push** | 设备作 RTSP 客户端推 MediaMTX（与 MJPEG **互斥**） |

默认上电为 **off**（不推流）。

## HTTP 接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/` | 控制页（发布模式 + 动作按钮；MJPEG 仅 stream 时显示） |
| GET | `/stream` | MJPEG（仅 `mode=stream`；否则 503） |
| GET | `/api/status` | JSON：`mode`、`push_status`、`push_url`、`stream_clients`、`has_jpeg`、`port` |
| POST | `/api/capture_mode?mode=off\|periodic\|stream\|rtsp_push` | 切换发布模式 |
| POST | `/api/vision_publish?mode=...` | 与 capture_mode 同源（供 C03 MQTT 对齐） |
| POST | `/api/cmd?cmd=...` | 投递狗指令，见下表 |
| GET | `/api/face` | 人脸框 + `local_id` / `display_name` |
| POST | `/api/face_enable?enabled=0\|1` | 开关人脸检测/识别 |
| POST | `/api/immich_config?api_key=...&api_url=...&delete_asset=0\|1` | Immich Key / 是否删临时图写入 NVS（S05；默认不删） |
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
