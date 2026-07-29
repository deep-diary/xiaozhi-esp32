# I01 · 控制输入架构

| 项 | 内容 |
|----|------|
| 域 | input / handle |
| 状态 | 需求 ready；固件 planned |
| 参考实现 | [touch_btn](../../touch_btn/) |

## 数据流

```text
Bluepad32 (Xbox BLE) ──┐
                       ├─► Normalize → HandleSnapshot
PC MQTT handle/input ──┘         │
                                 ▼
                          HandleEventHub
                    ┌────────────┴────────────┐
                    │                         │
             handle_mqtt                 队列 / Poll
             handle/status ↑          HandleAppDispatcher
                                          ├─ HandleAppLog
                                          ├─ HandleAppDog
                                          └─ HandleAppServo
```

- **Normalize**：各源映射到同一 `axes` / `buttons` 布局（与 YAML 一致；Xbox A/B/X/Y ↔ 抽象 a/b/x/y）。
- **Hub**：维护最新快照；Push 时可选通知 MQTT；业务走异步 fan-out。
- **App**：只消费快照 / 边沿事件，调用 `DogControl` / servo 等；**不**直接碰 Bluepad32 或 MQTT JSON。

## 快照模型（逻辑）

与 MQTT `handle/status` / `handle/input` 对齐：

| 字段 | 说明 |
|------|------|
| `connected` | 当前生效源是否在线 |
| `source` | `bt` \| `usb` \| `wifi` |
| `axes` | `lx,ly,rx,ry` ∈ [-1, 1] |
| `buttons` | `a,b,x,y,l1,r1,start,select` bool；`l2,r2` ∈ [0, 1] |
| `ts` | Unix 秒（或设备单调时钟换算） |

边沿（press / release）可由 App 对连续快照差分得到；v0.1 不强制单独 `HandleEvent` 枚举，实现时可仿 `TouchEvent` 增补。

## 多源策略

| 规则 | 说明 |
|------|------|
| 后到覆盖 | 任一源更新快照即成为当前控制请求 |
| `source` 标明 | status 上报当前生效源 |
| `handle/cmd` `disable` | 停止 App 执行（本地运控）；仍可更新 / 上报 status 供网页观察 |
| `enable` | 恢复 App 执行 |
| `pair` | 仅板载 BT 有意义（见 I02） |

## 与 touch 对照

| touch | handle |
|-------|--------|
| `TouchButtonController` | Bluepad32 适配 / MQTT input 适配 |
| `TouchEventHub` | `HandleEventHub` |
| `TouchAppDispatcher` | `HandleAppDispatcher` |
| `ITouchApp` | `IHandleApp` |
| `touch/status` 物理态 | `handle/status` 控制请求快照 |
| 无下行注入（除阈值 cmd） | **`handle/input` 下行注入**（PC 桥） |

`touch` 与 `handle` **并行存在**：三键手势与手柄互不替代。

## 板级装配（实现时）

- 代码建议目录：`main/boards/deep-dog/handle/`（镜像 `touch_btn/`）。
- 宏：`DEEP_DOG_HANDLE_ENABLE`、`DEEP_DOG_HANDLE_BT_ENABLE`、`DEEP_DOG_HANDLE_MQTT_INPUT_ENABLE`、各 `HANDLE_APP_*_ENABLE`。
- 注册点：`esp_sparkbot_board.cc`（与 touch 并列）。

## 验收（架构文档）

- [x] 双源汇入同一 Hub，业务仅经 App
- [x] MQTT status / input 与快照字段同构
- [ ] 固件目录与宏落地（实现任务）
