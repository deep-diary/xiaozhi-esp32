# 舵机控制模块 (Servo Control Module)

## 功能概述

本模块提供了PWM舵机的底层控制功能，包括：
- PWM信号生成
- 角度到脉宽转换
- 多舵机独立控制
- 角度范围限制

## 文件说明

### 核心文件

- **Servo.c/h**: 舵机底层驱动
  - PWM定时器配置
  - 脉宽调制控制
  - 角度设置接口

## 主要功能

### 1. 舵机初始化
```c
Servo_t servo;

// 初始化舵机 - GPIO引脚、最小角度、最大角度
esp_err_t result = Servo_init(&servo, gpio_num, min_angle, max_angle);
if (result != ESP_OK) {
    ESP_LOGE(TAG, "舵机初始化失败!");
    return;
}
```

### 2. 角度控制
```c
// 设置舵机角度
Servo_setAngle(&servo, angle);

// 获取当前角度
uint16_t current_angle = Servo_getAngle(&servo);
```

### 3. 脉宽控制
```c
// 直接设置脉宽（微秒）
Servo_setPulseWidth(&servo, pulse_width_us);
```

## 使用示例

### 标准180度舵机
```c
Servo_t servo;

// 初始化（GPIO18，角度范围0-180度）
Servo_init(&servo, 18, 0, 180);

// 设置到中间位置
Servo_setAngle(&servo, 90);

// 设置到最大角度
Servo_setAngle(&servo, 180);

// 设置到最小角度
Servo_setAngle(&servo, 0);
```

### 270度舵机
```c
Servo_t servo;

// 初始化（GPIO19，角度范围0-270度）
Servo_init(&servo, 19, 0, 270);

// 设置到中间位置
Servo_setAngle(&servo, 135);
```

### 自定义角度范围
```c
Servo_t servo;

// 初始化（GPIO20，自定义角度范围45-135度）
Servo_init(&servo, 20, 45, 135);

// 设置到中间位置
Servo_setAngle(&servo, 90);
```

## PWM参数配置

### 默认参数
- **PWM频率**: 50Hz（20ms周期）
- **最小脉宽**: 500μs（对应0度）
- **最大脉宽**: 2500μs（对应最大角度）
- **分辨率**: 14位（16384级）

### 脉宽-角度对应关系
```
角度 = (脉宽 - 500) / (2500 - 500) * (最大角度 - 最小角度) + 最小角度
```

例如，对于180度舵机：
- 0度 → 500μs
- 90度 → 1500μs
- 180度 → 2500μs

## API参考

### 初始化函数
```c
esp_err_t Servo_init(Servo_t* servo, gpio_num_t gpio, uint16_t min_angle, uint16_t max_angle);
```
- `servo`: 舵机对象指针
- `gpio`: GPIO引脚号
- `min_angle`: 最小角度
- `max_angle`: 最大角度
- 返回值: ESP_OK表示成功

### 角度设置
```c
void Servo_setAngle(Servo_t* servo, uint16_t angle);
```
- `servo`: 舵机对象指针
- `angle`: 目标角度（会自动限制在有效范围内）

### 获取当前角度
```c
uint16_t Servo_getAngle(const Servo_t* servo);
```
- 返回值: 当前设置的角度值

### 脉宽设置
```c
void Servo_setPulseWidth(Servo_t* servo, uint32_t pulse_width_us);
```
- `servo`: 舵机对象指针
- `pulse_width_us`: 脉宽值（微秒）

### 资源释放
```c
void Servo_deinit(Servo_t* servo);
```
- `servo`: 舵机对象指针

## 硬件连接

### 舵机接线
```
舵机         ESP32
-----       -------
VCC   →     5V (外部供电)
GND   →     GND
Signal →    GPIO_X
```

**重要提示**: 
- 舵机应使用外部5V电源供电，不要直接连接ESP32的3.3V
- 确保ESP32的GND与舵机电源的GND共地
- 推荐使用电容（如1000μF）在舵机电源端进行滤波

## 依赖关系

- **ESP-IDF LEDC驱动**: 用于PWM信号生成
- 无其他模块依赖，可独立使用

## 注意事项

1. **供电要求**:
   - 舵机需要5V外部供电
   - 电流需求：单个舵机至少500mA，多舵机时需要更大电流
   - 建议使用稳压电源模块

2. **角度范围**:
   - 不同型号舵机角度范围不同（常见：90°、180°、270°、360°）
   - 设置超出范围的角度会被自动限制

3. **PWM资源**:
   - ESP32有16个LEDC通道，可同时控制最多16个舵机
   - 每个舵机占用一个PWM通道

4. **性能优化**:
   - 避免频繁设置相同角度
   - 大角度变化时可能需要添加延时等待舵机到位

5. **机械限位**:
   - 请注意舵机的机械限位，避免卡死
   - 首次使用建议先小范围测试

## 常见问题

**Q: 舵机抖动怎么办？**
A: 可能是供电不足或信号干扰，建议：
- 检查电源容量
- 添加电源滤波电容
- 缩短信号线长度
- 添加信号线屏蔽

**Q: 舵机不动或角度不准？**
A: 可能原因：
- 检查接线是否正确
- 确认舵机型号和角度范围
- 调整脉宽参数（500-2500μs）
- 检查舵机是否损坏

**Q: 多个舵机同时控制时出现问题？**
A: 建议：
- 确保电源容量充足
- 分时控制，避免同时启动
- 使用更大容量的滤波电容

## 扩展应用

本模块可用于控制：
- 云台舵机
- 机械臂关节（小型）
- 机器人舵机
- 相机云台
- 其他PWM控制设备

## 相关模块

本模块被以下模块使用：
- **云台模块** (`../gimbal/`): 双轴云台控制

