# 示教 · teaching 子域

| 项 | 说明 |
|----|------|
| 固件 | [`motor/deep_motor_teaching.{h,cpp}`](../../../motor/deep_motor_teaching.cpp) · [`deep_motor`](../../../motor/deep_motor.cpp) 委托 |
| MQTT | [14-motor](../../mqtt/modules/14-motor.md) `motor/cmd` · `motor/teaching/*` |
| 前端 | [REQ-IOT-238](../../../../../../deep-trace/docs/requirements/features/iot/modules/motor/238-module-motor-teaching.md) |

## 文档索引

| ID | 文档 | 内容 |
|----|------|------|
| MOT-11 | [11-playback-mit.md](./11-playback-mit.md) | MIT 三阶段播放（过渡 + 轨迹） |
| MOT-12 | [12-record-v2.md](./12-record-v2.md) | `(t,pos,vel)` 录制、EPScan、MQTT 快照 |
| MOT-13 | [13-multi-axis.md](./13-multi-axis.md) | 6 轴并行 record/play |

## 关联

- 主动上报 T24：[07-active-report-frame.md](../07-active-report-frame.md)
- MQTT 字段表：[14-motor.md](../../mqtt/modules/14-motor.md)（正文仅索引，细节在本目录）

## 生命周期

```text
start → EPScan(50ms) + T24 → RX append TeachingSample → stop → 恢复 EPScan + MQTT snapshot
play  → 运控+enable → Phase A blend → Phase B 真实时间轴 MIT（× time_scale）
```
