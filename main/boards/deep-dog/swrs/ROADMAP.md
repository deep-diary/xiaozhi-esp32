# deep-dog 可追溯路线图

> 权威交付顺序。需求正文在 `dog/`、`vision/`；本文件只维护**序号、依赖、状态、验收指针**。

## 交付顺序（拍板）

1. **设备 = HTTP 服务器**：遥控 → MJPEG → 人脸框 → **本地数字 ID** → Immich 真名  
2. **设备 = 流媒体客户端**：MediaMTX 推流，**复用**同一套 `face_ai`  
3. 产品化：Kiosk / 对话个性化  

不并行把 Immich 与推流和本地 ID 绑死；视觉轨当前切片为 **V-S06**（分辨率）。

## 总表

| 序号 | ID | 文档 | 依赖 | 状态 |
|------|-----|------|------|------|
| D1～D5 | dog 历史 | [dog/DEVELOPMENT_PLAN.md](./dog/DEVELOPMENT_PLAN.md) | — | ✅ 主体完成 |
| D6 | 力矩保护 | 同上 §阶段六 | D1～D5 | 待办 |
| D7 | 大步插值/运控 | 同上 §阶段七 | D3 | 部分进行中 |
| D8 | 晃身/横移 | 同上 §阶段八 | D5 | ✅ |
| D9 | IMU | 同上 §阶段九 | — | 待办 |
| D11～D13 | RK3588 / ROS | 同上 | — | 远期 |
| **V-S01** | HTTP 狗控 | [vision/server/S01](./vision/server/S01-http-dog-motion.md) | DogControl | 主体 ✅ |
| **V-S02** | HTTP MJPEG | [vision/server/S02](./vision/server/S02-http-mjpeg.md) | S01 | ✅ |
| **V-S03** | 人脸框叠加 | [vision/server/S03](./vision/server/S03-http-face-overlay.md) | S02 | 一期 ✅ |
| **V-S04** | 本地数字 ID | [vision/server/S04](./vision/server/S04-local-face-numeric-id.md) | S03 | ✅ |
| **V-S05** | Immich 真名 | [vision/server/S05](./vision/server/S05-immich-real-name.md) | S04 | ✅ |
| **V-S06** | 预览/检测 VGA（640×480） | [vision/server/S06](./vision/server/S06-higher-resolution.md) | S02/S05 | ✅（真名待 Immich 队列空闲复验） |
| V-C01 | MediaMTX 验收 | [vision/client/C01](./vision/client/C01-stream-server-verify.md) | [infra](./vision/infra.md) | 待办 |
| V-C02 | 设备推流 | [vision/client/C02](./vision/client/C02-device-push-stream.md) | S04/S05、C01 | 待办（复用 face） |
| V-C03 | MQTT 推流开关 | [vision/client/C03](./vision/client/C03-mqtt-stream-control.md) | C02 | 待办 |
| V-C04 | MQTT 云台 | [vision/client/C04](./vision/client/C04-mqtt-gimbal.md) | C03 | 更后 |
| V-P01 | Kiosk / 对话 | [vision/product/P01](./vision/product/P01-kiosk-personalization.md) | S05 | 更后 |

## 依赖关系

```text
S01 → S02 → S03 → S04 → S05 → S06 → C02 → C03 → C04
                    │                      ↑
                    └── face_ai ───────────┘
C01 ─────────────────────────────────────→ C02
S05 ─────────────────────────────────────→ P01
```

## 视觉轨验收速查

| ID | 一句话验收 |
|----|------------|
| S01 | 网页可 init/前进等，不堵 HTTP |
| S02 | 可开关 MJPEG，拉流时仍可遥控 |
| S03 | Streaming + 人脸开 → Canvas 有框 |
| S04 | **不同人显示不同数字 ID**；同人稳定同一 ID；不调 Immich |
| S05 | 本地 ID 可绑定 Immich 真名；失败仍显示数字 ID |
| S06 | 预览/检测 **640×480**；Immich 裁剪短边 ≥320；拉流仍可遥控 |
| C02 | 设备推到 MediaMTX，外网/内网可拉；人脸逻辑复用 |

## 基础设施

见 [vision/infra.md](./vision/infra.md)（MediaMTX / EMQX / Immich，无明文密钥）。
