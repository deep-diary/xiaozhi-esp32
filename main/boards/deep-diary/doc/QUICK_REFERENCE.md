# 快速参考指南

## 目录速查

| 功能 | 目录 | 主要文件 | 文档 |
|------|------|----------|------|
| 电机控制 | `motor/` | `deep_motor.cpp/h` | [README](motor/README.md) |
| 机械臂 | `arm/` | `deep_arm.cc/h` | [README](arm/README.md) |
| 云台 | `gimbal/` | `Gimbal.c/h` | [README](gimbal/README.md) |
| 舵机 | `servo/` | `Servo.c/h` | [README](servo/README.md) |
| LED灯带 | `led/` | `led_control.cc/h` | [README](led/README.md) |
| CAN通信 | `can/` | `ESP32-TWAI-CAN.cpp` | [README](can/README.md) |
| 传感器 | `sensor/` | `QMA6100P/qma6100p.c` | [README](sensor/README.md) |
| 流媒体 | `streaming/` | `mjpeg_server.cc/h` | [README](streaming/README.md) |

## 常用操作速查

### 电机控制

```cpp
// 初始化
DeepMotor* motor = new DeepMotor(led_strip);
motor->registerMotor(1);

// 使能电机
motor->enable(1);

// 位置控制
motor->setTargetPosition(1, 90.0f, 10.0f);

// 获取角度
float angle = motor->getCurrentAngle(1);
```

**详细说明**: [motor/README.md](motor/README.md)

---

### 机械臂控制

```cpp
// 初始化
uint8_t motor_ids[6] = {1, 2, 3, 4, 5, 6};
DeepArm* arm = new DeepArm(motor, motor_ids);

// 使能
arm->enableAll();

// 设置姿态
float pose[6] = {0, 30, 45, 0, 90, 0};
arm->setPose(pose);

// 录制动作
arm->startRecording();
// ... 操作
arm->stopRecording();
arm->saveRecording();

// 播放动作
arm->loadRecording();
arm->playRecording();
```

**详细说明**: [arm/README.md](arm/README.md)

---

### 云台控制

```cpp
// 初始化
Gimbal_t gimbal;
Gimbal_init(&gimbal, 19, 20);  // GPIO19(Pan), GPIO20(Tilt)

// 设置角度
Gimbal_setAngles(&gimbal, 135, 90);  // 水平135°, 垂直90°

// 单独控制
Gimbal_setPan(&gimbal, 180);   // 水平180°
Gimbal_setTilt(&gimbal, 120);  // 垂直120°
```

**详细说明**: [gimbal/README.md](gimbal/README.md)

---

### 舵机控制

```cpp
// 初始化
Servo_t servo;
Servo_init(&servo, 18, 0, 180);  // GPIO18, 0-180°

// 设置角度
Servo_setAngle(&servo, 90);

// 获取角度
uint16_t angle = Servo_getAngle(&servo);
```

**详细说明**: [servo/README.md](servo/README.md)

---

### LED灯带控制

```cpp
// 初始化
CircularStrip* led = new CircularStrip(8, 12);  // GPIO8, 12个LED

// 设置颜色
led->Fill(255, 0, 0);  // 全红
led->Refresh();

// 动画效果
led->Breathe(255, 0, 255, 2000);  // 紫色呼吸
led->Rainbow(50);                  // 彩虹效果
```

**详细说明**: [led/README.md](led/README.md)

---

### CAN通信

```cpp
// 初始化
ESP32Can.begin(ESP32Can.convertSpeed(1000), 4, 5, 10, 10);

// 发送
CanFrame txFrame;
txFrame.identifier = 0x123;
txFrame.extd = 0;
txFrame.data_length_code = 8;
// ... 填充数据
ESP32Can.writeFrame(txFrame, 1000);

// 接收
CanFrame rxFrame;
if (ESP32Can.readFrame(rxFrame, 1000)) {
    // 处理数据
}
```

**详细说明**: [can/README.md](can/README.md)

---

### 加速度计

```cpp
// 初始化
qma6100p_init(i2c_bus);

// 读取数据
qma6100p_rawdata_t data;
qma6100p_read_rawdata(&data);

// 访问数据
ESP_LOGI(TAG, "加速度: X=%.2f, Y=%.2f, Z=%.2f", 
         data.acc_x, data.acc_y, data.acc_z);
ESP_LOGI(TAG, "角度: Pitch=%.1f°, Roll=%.1f°", 
         data.pitch, data.roll);
```

**详细说明**: [sensor/README.md](sensor/README.md)

---

### MJPEG视频流

```cpp
// 初始化
auto mjpeg = std::make_unique<MjpegServer>(8080);
mjpeg->SetFrameRate(10);
mjpeg->SetJpegQuality(80);

// 启动服务器
mjpeg->Start();

// 访问地址
ESP_LOGI(TAG, "URL: %s", mjpeg->GetUrl().c_str());
```

**浏览器访问**: `http://<ESP32_IP>:8080/stream`

**详细说明**: [streaming/README.md](streaming/README.md)

---

## GPIO配置速查

| 功能 | GPIO | 说明 |
|------|------|------|
| CAN TX | 4 | CAN总线发送 |
| CAN RX | 5 | CAN总线接收 |
| I2C SDA | 6 | I2C数据线 |
| I2C SCL | 7 | I2C时钟线 |
| WS2812 | 8 | LED灯带数据 |
| 按键 | 0 | Boot按键 |
| 内置LED | 46 | 状态指示 |
| 舵机Pan | 19 | 云台水平舵机 |
| 舵机Tilt | 20 | 云台垂直舵机 |

**完整配置**: 参考 `config.h`

---

## 功能开关速查

在 `config.h` 中配置：

```cpp
// 启用/禁用功能
#define ENABLE_CAMERA_FEATURE      1  // 相机
#define ENABLE_CAN_FEATURE         1  // CAN总线
#define ENABLE_LED_STRIP_FEATURE   1  // WS2812灯带
#define ENABLE_QMA6100P_FEATURE    1  // QMA6100P加速度计
```

---

## MCP工具速查

### 电机工具
- `motor_enable` - 使能电机
- `motor_disable` - 失能电机
- `motor_set_position` - 设置位置
- `motor_set_speed` - 设置速度
- `motor_get_status` - 获取状态

### 机械臂工具
- `arm_enable` - 使能机械臂
- `arm_disable` - 失能机械臂
- `arm_set_pose` - 设置姿态
- `arm_get_pose` - 获取姿态
- `arm_start_recording` - 开始录制
- `arm_play_recording` - 播放录制

### 云台工具
- `gimbal_set_angle` - 设置云台角度
- `gimbal_set_pan` - 设置水平角度
- `gimbal_set_tilt` - 设置垂直角度
- `gimbal_reset` - 重置到中间位置

### LED工具
- `led_set_color` - 设置颜色
- `led_set_brightness` - 设置亮度
- `led_breathe` - 呼吸效果
- `led_rainbow` - 彩虹效果
- `led_clear` - 清空

---

## 常见问题速查

### Q: 编译错误找不到头文件？
A: 检查include路径是否正确：
```cpp
#include "motor/deep_motor.h"        // ✅ 正确
#include "deep_motor.h"              // ❌ 错误
```

### Q: 电机不响应？
A: 检查清单：
1. CAN总线是否初始化成功
2. 电机是否已注册 `registerMotor(id)`
3. 电机是否已使能 `enable(id)`
4. CAN收发器连接是否正确
5. 终端电阻是否安装

### Q: 视频流无法访问？
A: 检查清单：
1. WiFi是否连接
2. MJPEG服务器是否启动
3. IP地址是否正确
4. 防火墙是否开放8080端口
5. 相机是否初始化成功

### Q: 加速度计读取失败？
A: 检查清单：
1. I2C总线是否初始化
2. QMA6100P是否连接
3. I2C地址是否正确（0x12）
4. 上拉电阻是否安装

### Q: LED灯带不亮？
A: 检查清单：
1. WS2812供电是否正常（5V）
2. 数据线连接是否正确
3. LED数量配置是否正确
4. 是否调用了Refresh()

---

## 调试技巧速查

### 启用详细日志
```cpp
esp_log_level_set("atk_dnesp32s3", ESP_LOG_DEBUG);
esp_log_level_set("DeepMotor", ESP_LOG_DEBUG);
esp_log_level_set("DeepArm", ESP_LOG_DEBUG);
esp_log_level_set("mjpeg_server", ESP_LOG_DEBUG);
```

### 监控内存使用
```cpp
ESP_LOGI(TAG, "Free heap: %lu", esp_get_free_heap_size());
ESP_LOGI(TAG, "Min free heap: %lu", esp_get_minimum_free_heap_size());
```

### 监控任务状态
```cpp
char buffer[1024];
vTaskList(buffer);
ESP_LOGI(TAG, "Tasks:\n%s", buffer);
```

---

## 性能参数速查

| 参数 | 值 |
|------|---|
| CPU频率 | 240MHz (双核) |
| RAM | 512KB SRAM + 8MB PSRAM |
| Flash | 16MB |
| WiFi | 2.4GHz 802.11n |
| CAN波特率 | 最高1Mbps |
| 视频帧率 | 最高25fps (QVGA) |
| 音频采样率 | 16kHz输入 / 48kHz输出 |

---

## 文档导航

- **项目总览**: [README.md](README.md)
- **重构总结**: [REFACTORING_SUMMARY.md](REFACTORING_SUMMARY.md)
- **本快速参考**: [QUICK_REFERENCE.md](QUICK_REFERENCE.md)

### 模块文档
- [电机控制](motor/README.md)
- [机械臂控制](arm/README.md)
- [云台控制](gimbal/README.md)
- [舵机控制](servo/README.md)
- [LED控制](led/README.md)
- [CAN通信](can/README.md)
- [传感器](sensor/README.md)
- [流媒体](streaming/README.md)

### 技术文档
详见 `doc/` 目录下的各类技术文档

---

**最后更新**: 2025年10月21日

