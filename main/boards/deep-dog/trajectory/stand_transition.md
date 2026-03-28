# Stand Transition Trajectory

## 背景

从卧倒到站立若只下发一个目标点，会出现较大的机械冲击。当前 deep-dog 已改为固定时长插值轨迹。

## 当前实现

- 入口：`DogControl::stand()`
- 规划：`trajectory_plan_linear_fixed_duration(...)`
- 关节数：12（四条腿 * 三关节）
- 点数：`DEEP_DOG_POSE_INTERP_POINTS`
- 总时长：`DEEP_DOG_POSE_INTERP_DURATION_MS`

轨迹执行按轨迹时间戳差值延时，因此在总时长不变时：

- 增加点数 -> 单点间隔自动减小
- 减少点数 -> 单点间隔自动增大

## 相关宏

位于 `config.h`：

- `DEEP_DOG_POSE_INTERP_POINTS`
- `DEEP_DOG_POSE_INTERP_DURATION_MS`
- `DEEP_DOG_MIT_VDES_RAD_S`

## 零速保持

站立完成后会补发一次 `v_des=0` 的保持帧，降低停稳阶段的速度分量偏移风险。
