# 服务器人脸识别响应需求及协议

**文档用途**：供服务器端（xiaozhi-esp32-server）开发人脸识别 HTTP 响应时使用，与设备端 Deep Thumble 人脸识别功能对接。

**参考**：
- 设备端需求：`main/boards/deep-thumble/SWRS.md` 第 2 章（人脸识别）、第 8 章（服务器协议对接）
- 服务器现有实现：`vision_handler.py`（MCP Vision POST 处理）

---

## 1. 概述

### 1.1 背景

- 设备端（Deep Thumble）在**本地人脸识别失败**时，会调用视觉解释接口并传入问题 `"人脸识别"`，携带当前摄像头图像。
- 服务器需在**现有 Vision 接口**的 HTTP 响应中，增加 **人脸识别结果** 字段，供设备端解析并更新本地人脸姓名。

### 1.2 对接方式

- **入口**：设备端调用 `Esp32Camera::Explain("人脸识别")`，即向服务器发送 **MCP Vision POST** 请求。
- **响应**：服务器在**同一 HTTP 响应的 JSON Body** 中返回视觉分析结果，并**扩展** `face_recognition` 字段。
- **不依赖**：设备端以**同步解析 HTTP 响应 JSON** 为主，不依赖 WebSocket 或后续消息。

### 1.3 原则

- **向后兼容**：保留现有字段 `success`、`action`、`response`，仅新增可选字段 `face_recognition`。
- **可选实现**：未接入人脸识别（如未配置 Immich）时，可不返回 `face_recognition`，或返回 `detected: false`。

---

## 2. HTTP 请求（设备 → 服务器）

设备端已按现有 MCP Vision 约定发送，服务器无需改请求解析逻辑。

| 项目       | 说明 |
|------------|------|
| 方法       | POST |
| Content-Type | multipart/form-data |
| 认证       | Header: `Authorization: Bearer <token>` |
| 设备标识   | Header: `Device-Id`、`Client-Id` |
| 表单字段   | `question`：字符串，设备端传 `"人脸识别"`；`image`：图片文件（二进制） |

**说明**：当 `question == "人脸识别"` 时，设备端期望在响应中解析 `face_recognition` 并更新本地人脸库。

---

## 3. HTTP 响应协议

### 3.1 现有字段（必须保留）

当前 `vision_handler.py` 返回格式为：

```json
{
  "success": true,
  "action": "RESPONSE",
  "response": "AI分析的文本结果，用于TTS播放和显示"
}
```

- `success`：布尔，请求是否成功。
- `action`：字符串，固定 `"RESPONSE"`。
- `response`：字符串，视觉/LLM 的文本结果，用于设备端 TTS 与展示。

以上字段必须保留，且语义不变。

### 3.2 扩展字段：face_recognition（人脸识别结果）

在**同一 JSON 根对象**下增加可选对象 `face_recognition`。

#### 3.2.1 完整响应示例

```json
{
  "success": true,
  "action": "RESPONSE",
  "response": "这是AI分析的文本结果，用于TTS播放和显示",
  "face_recognition": {
    "detected": true,
    "people": [
      {
        "person_id": "immich_person_id_1",
        "name": "张三",
        "confidence": 0.95
      },
      {
        "person_id": "immich_person_id_2",
        "name": "李四",
        "confidence": 0.88
      }
    ]
  }
}
```

#### 3.2.2 face_recognition 字段说明

| 字段        | 类型   | 必需 | 说明 |
|-------------|--------|------|------|
| `detected`  | boolean | 是  | 是否检测到人脸（或是否进行了人脸识别）。未做识别时可设为 `false`。 |
| `people`    | array  | 是  | 识别到的人物列表，可为空数组 `[]`。 |

#### 3.2.3 people[] 元素字段说明

| 字段         | 类型   | 必需 | 说明 |
|--------------|--------|------|------|
| `name`       | string | 是  | 人物姓名。见下方「name 取值规则」。 |
| `person_id`  | string | 否  | 服务器/Immich 侧人物 ID，用于服务端关联，设备端可选使用。 |
| `confidence` | number | 否  | 识别置信度，建议 0.0～1.0。 |

**name 取值规则**（设备端据此更新本地人脸姓名）：

- 识别到且已命名：返回真实姓名，如 `"张三"`。
- 识别到但未命名（Immich 未命名）：返回 `"未命名"` 或 `null`，设备端会按 unknown 系列处理。
- 无法识别或识别失败：返回 `"unknown"`。
- 建议：空或缺失时设备端按 `"unknown"` 处理，因此服务器可统一用 `"unknown"` 表示未知。

### 3.3 特殊情况与推荐响应

| 场景                 | detected | people        | 说明 |
|----------------------|----------|---------------|------|
| 未检测到人脸         | false    | []            | 可不做人脸识别或识别结果为空。 |
| 检测到人脸但无法识别 | true     | [{"name": "unknown"}] | 仅一人且未知。 |
| 检测到多人，部分未命名 | true     | 见下方示例    | 已命名与未命名可混在同一列表。 |
| 未接入人脸识别       | 不返回 face_recognition，或返回 `detected: false, people: []` | 保持兼容。 |

多人且部分未命名示例：

```json
"face_recognition": {
  "detected": true,
  "people": [
    { "person_id": "id1", "name": "张三", "confidence": 0.95 },
    { "person_id": "id2", "name": "未命名", "confidence": 0.7 }
  ]
}
```

### 3.4 错误响应（与现有一致）

请求失败时仍使用现有错误格式，**不需要**包含 `face_recognition`：

```json
{
  "success": false,
  "message": "错误描述"
}
```

---

## 4. 服务器端实现要点

### 4.1 参考代码位置

- 文件：`vision_handler.py`
- 方法：`handle_post`
- 修改点：在构建并返回 `return_json` 之前，根据**现有人脸识别结果**（如 Immich 返回的 `people_info`）拼装 `face_recognition` 并写入 `return_json`。

### 4.2 现有 vision_handler 中可用数据

- 调用 Immich 后得到 `people_info`：
  - `people_info.get("success")`：是否成功。
  - `people_info.get("people")`：人物列表，元素通常包含 `person_name`、`person_id` 等（以实际 Immich API 为准）。
- 已有逻辑中已区分「已命名」与「未命名」（如过滤 `"未命名"`），可复用同一数据源构建 `face_recognition.people`。

### 4.3 构建 face_recognition 的推荐逻辑

1. **无人脸识别或识别失败**  
   - 设置 `face_recognition = { "detected": false, "people": [] }`，或整段不返回 `face_recognition`（设备端按无结果处理）。

2. **有人脸识别结果**  
   - 设置 `detected: true`。  
   - 遍历 `people_info["people"]`，对每人构造对象：
     - `name`：优先使用 Immich 的 `person_name`；未命名或空则用 `"未命名"` 或 `"unknown"`。
     - `person_id`：若有则填 Immich `person_id`。
     - `confidence`：若 Immich 有则填，否则可省略。
   - 将上述对象列表赋给 `face_recognition.people`。

3. **写入响应**  
   - 在现有 `return_json = { "success": True, "action": ..., "response": result }` 之后增加一行：  
     `return_json["face_recognition"] = face_recognition`  
   - 再返回 `web.Response` 的 JSON。

### 4.4 与现有逻辑的一致性

- **不要**改变现有 `success`、`action`、`response` 的生成方式（包括 VLLM 调用、question 拼接人物信息等）。
- **不要**改变认证、multipart 解析、图片校验、Immich 上传与识别的现有流程。
- 仅在同一响应体中**增加** `face_recognition` 字段，保证设备端与现有客户端行为兼容。

---

## 5. 设备端使用方式（供联调参考）

- 设备端在收到 HTTP 200 后，解析 Body 为 JSON。
- 若存在 `face_recognition` 且 `face_recognition.detected === true` 且 `face_recognition.people` 非空：
  - 遍历 `people`，取每个元素的 `name`。
  - 用 `name` 匹配本地人脸 ID（如 `FindFaceIdByName`），若匹配则调用 `SetFaceName(face_id, name)`；若不匹配则视为新人脸，用 `GetUnrecognizedFaceId()` 得到 ID 再 `SetFaceName`。
- 设备端不依赖 `person_id` 做本地人脸绑定，`person_id` 仅便于服务器或 Web 端关联。

---

## 6. 检查清单（服务器开发完成后可自测）

- [ ] 人脸识别成功时，HTTP 200 且 JSON 中包含 `face_recognition`，且 `detected` 为 true，`people` 为非空数组。
- [ ] 每人至少包含 `name` 字段；已命名人物为真实姓名，未命名为 `"未命名"` 或 `"unknown"`。
- [ ] 未检测到人脸或未做识别时，`face_recognition.detected` 为 false 或未返回 `face_recognition`，且 `success`、`action`、`response` 仍正常。
- [ ] 请求失败时仍返回 `success: false` 及 `message`，且不依赖 `face_recognition`。
- [ ] 现有 Vision 能力（非人脸识别场景）行为不变，且不依赖 `face_recognition` 的客户端不受影响。

---

## 7. 文档版本

| 版本 | 日期       | 说明     |
|------|------------|----------|
| 1.0  | 2025-01-XX | 初版，与 SWRS 8.x 及 vision_handler 对齐 |

本文档与设备端 `main/boards/deep-thumble/SWRS.md` 第 8 章保持一致，若设备端协议有更新，请同步更新本文档。
