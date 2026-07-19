# V-S01 · HTTP Server：狗子动作控制

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-S01**（[ROADMAP](../../ROADMAP.md)） |
| 优先级 | P0 |
| 下一切片 | [S02 MJPEG](./S02-http-mjpeg.md) |
| 代码落点 | `main/boards/deep-dog/http-server/` → `DogControl`（经 `dog_web_cmd`） |
| 基线状态 | **主体已实现** |

## 1. 背景

局域网浏览器遥控与 MCP/触摸同一套 `DogControl`；**禁止在 httpd 回调里长时间跑 CAN/步态**。

## 2. 目标

- 默认端口 **8080**：动作控制 + 状态查询。
- 指令语义与 MCP/触摸对齐（可子集），有明确 UI 反馈。

## 3. 范围

**包含**：控制页动作区、`POST /api/cmd`、`dog_initialized`、`GET /api/dog_status`、队列可观测。  
**不包含**：MJPEG（S02）、人脸（S03+）、鉴权、新步态算法。

## 4. 接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/` | 控制页 |
| POST | `/api/cmd?cmd=` | `init`/`forward`/`back`/`stand`/`liedown`/`dance`/`stop_walk`/`disable` |
| GET | `/api/status` | 含 `dog_initialized` |
| GET | `/api/dog_status` | `motors[]` + `torque_limit_nm` + `has_fault` |

`torque_limit` 开启时仍须返回完整 `motors[]`。

## 5. 功能需求

| ID | 需求 | 基线 |
|----|------|------|
| DOG-01 | 队列执行，不阻塞 httpd | ✅ |
| DOG-02 | 上表 cmd 全集 | ✅ |
| DOG-03 | 初始化门控 UI | ✅ |
| DOG-04 | 指令反馈文案 | ✅ |
| DOG-05 | dog_status 完整 motors | ⚠️ |
| DOG-06 | 与 MCP/触摸共存 | ✅ |
| DOG-07 | `DEEP_DOG_HTTP_SERVER_ENABLE` / PORT | ✅ |

## 6. 验收

- [ ] 未 init 仅可初始化；init 后可前进等动作
- [ ] 连点不卡死 HTTP；可见 `dog_web_cmd` 日志
- [ ] 有/无力矩阈值时 `motors[]` 合理
