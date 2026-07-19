# 视频流上传（已拆分）

> **本文已拆分为多份 SWRS，请勿在此继续追加需求。**  
> 索引见 [README.md](./README.md)。

原始草稿中的内容映射如下：

| 原始条目 | 新文档 |
|----------|--------|
| MediaMTX / EMQX 服务器信息 | [00-shared-infra.md](./00-shared-infra.md) |
| 不结合硬件确认推拉流 | [01-stream-server-verify.md](./01-stream-server-verify.md) |
| 板端视频流上传、开机上传、拉流验收 | [02-camera-stream-upload.md](./02-camera-stream-upload.md) |
| MQTT 控制视频流开关 | [03-mqtt-stream-control.md](./03-mqtt-stream-control.md) |
| MQTT 云台（GPIO 38/48 PWM） | [04-mqtt-gimbal-control.md](./04-mqtt-gimbal-control.md) |
| 本地人脸检测与上传 | [05-face-detect-upload.md](./05-face-detect-upload.md) |
| Immich 识别、回传设备、通知前端 | [06-face-recognize-immich.md](./06-face-recognize-immich.md) |
| Kiosk 轮播 + 个人档案对话 | [07-kiosk-personalization.md](./07-kiosk-personalization.md) |

## 实现时请注意

1. **不需要底盘**：删除板内 UART/底盘逻辑；**GPIO 38/48 专用于云台 PWM**，见 [04](./04-mqtt-gimbal-control.md)。
2. **人脸大图走 HTTP，控制/事件走 MQTT**，见 05 / 00。
3. 设备 MQTT **首选内网** `192.168.31.25:1883`，不要依赖公网 `mqtt-tcp` 裸连。
