# MOT-12 · 示教录制 v2（t, pos, vel + EPScan + 快照）

| 项 | 内容 |
|----|------|
| ID | MOT-12 |
| 状态 | ready |
| 固件 | `MotorTeachingManager` in [`deep_motor_teaching.cpp`](../../../motor/deep_motor_teaching.cpp) |
| 关联 | MOT-11 播放 · MOT-07 T24 · 14-motor MQTT |

## 1. 数据结构

```cpp
struct TeachingSample {
    uint32_t t_ms;           // 相对录制起点的单调毫秒
    float position_rad;
    float velocity_rad_s;    // T24 反馈 current_speed
};
```

每电机独立 `TeachingTrack`，最多 `MAX_TEACHING_POINTS`（300）点。

## 2. 采样 · EPScan 0x7026

| n | 周期 ms | 频率 |
|---|---------|------|
| 1 | 10 | 100Hz |
| 9 | 50 | 20Hz |

公式：`period_ms = 10 + (n-1)×5`

`startTeaching`：写 `PARAM_EPScan_time=0x7026` → n=9（50ms）→ `requestActiveReport`。  
`stopTeaching`：恢复 n=1（10ms 默认）。

## 3. 播放 · 真实时间轴

| 参数 | 默认 | 说明 |
|------|------|------|
| `blend_ms` | 3000 | Phase A：当前角 → 首点 |
| `time_scale` | 1.0 | 1.0=原速；2.0=两倍慢 |
| `use_recorded_timeline` | true | 有 v2 样本时按 `t_ms` 播放 |
| `duration_ms` | 10000 | timeline 关闭时的均匀拉时（MOT-11 兼容） |
| `kp/kd/tau_ff` | 1/1/0 | MIT 增益 |

Phase B：插值 `(pos, vel)` 来自录制；`vel` 乘以 `1/time_scale`。

## 4. MQTT 快照（Phase B）

### `motor/teaching/snapshot` ↑ QoS0

stop 录制后发布（单电机或 multi）：

```json
{
  "motor_id": 1,
  "point_count": 120,
  "duration_ms": 5950,
  "sample_period_ms": 50,
  "samples": [{ "t_ms": 0, "pos": 0.1, "vel": 0.0 }],
  "ts": 1700000000
}
```

Multi：`motors: [{ motor_id, samples, ... }]` 替代顶层 `motor_id`。

### `motor/teaching/status` ↑ retain QoS0

摘要：`recording`, `ready`, `motor_id`, `point_count`, `duration_ms`。

## 5. API

```cpp
bool startTeaching(uint8_t motor_id, const TeachingRecordConfig* cfg = nullptr);
bool startTeachingMulti(const uint8_t* motor_ids, uint8_t count, const TeachingRecordConfig* cfg = nullptr);
bool stopTeaching();
bool executeTeaching(uint8_t motor_id, const TeachingPlayConfig* cfg = nullptr);
bool executeTeachingMulti(const uint8_t* motor_ids, uint8_t count, const TeachingPlayConfig* cfg = nullptr);
```

MQTT `motor/cmd` 增：`play_time_scale`、`sample_period_ms`（start 时可选）、`teaching_motor_ids`（multi）。

## 6. 验收

- [ ] 50ms EPScan 下 300 点 ≈ 15s；样本含 vel
- [ ] 播放按真实时间轴 × time_scale
- [ ] stop 后 EPScan 恢复；MQTT snapshot 可解析
- [ ] 6 轴 start/stop/play multi
