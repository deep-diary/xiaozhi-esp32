# MOT 实机验收清单

本环境无 `idf.py`，固件需本地编译烧录后勾选。

## 1. 剖面

- [ ] 串口：`ext_pins mode=can A=38 B=48 can=1 motor=1 dog=0 … gimbal=0`
- [ ] MQTT `device/info`：`ext_pins.mode=can`，`capabilities.can/motor=true`，`gimbal/servo=false`

## 2. CAN（MOT-02）

```json
{ "tunnel": true, "allow_tx": false, "max_hz": 50, "batch_max": 32, "ext_only": true }
```

- [ ] `can/status` retain 可见
- [ ] 电机有反馈时 `can/frames` 有扩展帧
- [ ] `allow_tx=false` 时 `can/tx` 被拒

## 3. Motor（MOT-03）

```json
{ "motor_id": 1, "enable": true, "position_rad": 1.0, "speed_limit": 5.0 }
```

- [ ] `motor/status` 出现 id=1 反馈或至少 ok=true
- [ ] 位置变化可感知

## 4. Handle motor + 轴（MOT-04/05 · I08b）

```json
{ "action": "set_profile", "profile": "motor", "persist": true }
```

- [ ] `handle/keymap`：`schema_ver=5`，含 `bindable_axes` / `axis_bindings`，`rx→motor.pos_norm`
- [ ] A 使能、B 失能；右摇杆满偏≈±12.57、回中→0
- [ ] `set_profile=led_demo`：轴面板仍有 `axis_bindings`（可全 none），不崩

## 需求出处

- `swrs/motor/01`～`05` · `swrs/input/I08b` · `mqtt/modules/12-can` · `14-motor`
