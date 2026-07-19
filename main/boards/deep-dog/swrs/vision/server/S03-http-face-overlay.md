# V-S03 · HTTP 视频流 + 网页人脸框

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-S03** |
| 优先级 | P1 |
| 依赖 | [S02](./S02-http-mjpeg.md)（**Streaming** 下送帧） |
| 下一切片 | [S04 本地数字 ID](./S04-local-face-numeric-id.md) |
| 代码落点 | `face_ai/` + `http-server/`；**禁止** httpd 内推理 |
| 基线状态 | **一期已实现** |

## 1. 目标

Streaming + 启用人脸 → Canvas 叠框；关人脸停推理；与狗控解耦。

## 2. 方案 B（已实现）

MJPEG 不画框；轮询 `GET /api/face`；`POST /api/face_enable`。

```text
Streaming → RGB565 → face_ai 检测 → FaceSnapshot
         → JPEG → /stream
Browser: Canvas 叠框
```

## 3. 范围

**包含**：检测、框、开关、节流。  
**不包含**：本地数字 ID（S04）、Immich（S05）、服务端画框、跟脸。

## 4. 功能 / 验收

| ID | 基线 | 说明 |
|----|------|------|
| FACE-01～07 | ✅ | 推流可开人脸、可见框、可关、非 Streaming 不送帧等 |
| FACE-08 | ⚠️ | 暗场误检抑制 |

- [ ] 真人出镜有框，`has_face=true`
- [ ] 关人脸/关流后停检测
- [ ] 拉流+人脸时仍可遥控（S01）

## 5. 后续

身份识别从 **S04** 开始（仅数字 ID，不调 Immich）。
