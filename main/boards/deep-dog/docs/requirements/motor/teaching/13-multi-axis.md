# MOT-13 · 示教多轴（6 轴臂）

| 项 | 内容 |
|----|------|
| ID | MOT-13 |
| 状态 | ready |
| 固件 | `startTeachingMulti` / `executeTeachingMulti` |
| 前端 | REQ-IOT-239（后续；首轮 MOT-12 仅固件 API） |

## 行为

- 每电机独立 `TeachingTrack`
- `startTeachingMulti`：对各 ID 依次 reset + EPScan + activeReport
- `stopTeaching`：停止全部 recording 槽，打包 multi snapshot
- `executeTeachingMulti`：单 `playTask` 轮询多路 MIT，统一主时间轴（最长 track）

## MQTT

`motor/cmd` 可选 `teaching_motor_ids: [1,2,3,4,5,6]`，与 `teaching: start|play` 联用。

Snapshot：`{ "motors": [ { "motor_id", "samples", ... } ], "ts" }`。
