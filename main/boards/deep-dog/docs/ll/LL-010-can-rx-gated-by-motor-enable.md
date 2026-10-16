# LL-010：CAN 收帧任务被 MOTOR_ENABLE 门控——关电机后"CAN 看着还在，实际只剩半条链路"

- **编号**：LL-010
- **日期**：2026-10-16
- **关键词**：CAN RX / CanRxTask / MOTOR_ENABLE / tunnel 无帧 / Bus-Off 不自愈 / 半条链路 / CFG-03

## 现象

把 `DEEP_DOG_MOTOR_ENABLE` 改 0（保留 `CAN_ENABLE=1`）后，CAN 看起来完全正常：

- 网页注入 `cmd/can/tx` 能发出去；
- `status/can` retain 正常，2 s 一次的联调心跳（模拟 RX）照旧；
- 但**总线真实报文一条都收不到**（`status/can.rx_frames` 恒为 0）；
- 总线异常（Bus-Off）后**不再自愈**，串口没有 `CAN bus-off: recovery attempt …`。

极易被误判为"接线/收发器/对端没发"，实际是固件把读帧者整条关掉了。

## 根因

**唯一的真实总线读帧者** `CanRxTask`（`ESP32Can.readFrame` → `DeepDogCanNotifyFrame` → `can_frame_hub` → MQTT tunnel）
其**定义与创建**都被 `#if DEEP_DOG_CAN_ENABLE && DEEP_DOG_MOTOR_ENABLE` 双重门控；
而 TX 与心跳在 `mqtt/modules/can_mqtt.cc` 里**只依赖 `CAN_ENABLE`**（直接用 `ESP32Can.writeFrame`）。
于是就出现"发得出、心跳有、收不到"的半条链路，且 Bus-Off 守护随任务一起消失。

## 正确做法

收帧链路只依赖 CAN，不依赖电机栈：

```c
// 1) 成员/任务定义/任务创建：门控放宽为只看 CAN
#if DEEP_DOG_CAN_ENABLE
...
#endif

// 2) 任务参数：无电机栈时传 nullptr（函数体内已有 if (motor) 判空）
void* can_rx_arg = nullptr;
#if DEEP_DOG_MOTOR_ENABLE
can_rx_arg = deep_motor_;
#endif
xTaskCreate(CanRxTask, "can_rx", CAN_RX_TASK_STACK, can_rx_arg, CAN_RX_TASK_PRIO, &can_rx_task_handle_);

// 3) 函数体内电机相关分支（含 DeepMotor 类型引用）再单独套 #if DEEP_DOG_MOTOR_ENABLE
```

验证：`MOTOR=0` 剖面下台架注入一帧 → `status/can/frames` 应出现该帧；再断总线制造 Bus-Off，应出现自愈日志。

## 相关文件

- `main/boards/deep-dog/esp_sparkbot_board.cc`（`can_rx_task_handle_` 成员 / `CanRxTask` 定义 / `xTaskCreate`）
- `main/boards/deep-dog/board_features.h`（`DEEP_DOG_PROFILE` 剖面开关）
- `main/boards/deep-dog/docs/requirements/config/CFG-03-compile-time-profiles.md`
