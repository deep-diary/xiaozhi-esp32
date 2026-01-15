# 传感器模块 (Sensor Module)

## 功能概述

本模块提供了各类传感器的驱动和接口，目前包含：
- **QMA6100P**: 三轴加速度计

未来可扩展支持：
- 陀螺仪
- 温湿度传感器
- 气压传感器
- 距离传感器等

## 目录结构

```
sensor/
├── README.md                   # 本文件
├── README_QMA6100P.md         # QMA6100P详细文档
├── INTEGRATION_QMA6100P.md    # QMA6100P集成指南
└── QMA6100P/                  # QMA6100P驱动
    ├── qma6100p.c
    └── qma6100p.h
```

## QMA6100P 加速度计

### 功能特性
- 三轴加速度测量
- 俯仰角和翻滚角计算
- I2C通信接口
- 低功耗模式

### 快速开始

#### 1. 初始化
```c
#include "QMA6100P/qma6100p.h"

// 初始化QMA6100P（传入I2C总线句柄）
esp_err_t ret = qma6100p_init(i2c_bus);
if (ret == ESP_OK) {
    ESP_LOGI(TAG, "QMA6100P初始化成功");
} else {
    ESP_LOGE(TAG, "QMA6100P初始化失败");
}
```

#### 2. 读取数据
```c
qma6100p_rawdata_t accel_data;

// 读取加速度计原始数据
qma6100p_read_rawdata(&accel_data);

// 访问数据
ESP_LOGI(TAG, "加速度: X=%.2f, Y=%.2f, Z=%.2f m/s²", 
         accel_data.acc_x, accel_data.acc_y, accel_data.acc_z);
ESP_LOGI(TAG, "角度: 俯仰=%.1f°, 翻滚=%.1f°", 
         accel_data.pitch, accel_data.roll);
```

#### 3. 数据结构
```c
typedef struct {
    float acc_x;    // X轴加速度 (m/s²)
    float acc_y;    // Y轴加速度 (m/s²)
    float acc_z;    // Z轴加速度 (m/s²)
    float pitch;    // 俯仰角 (度)
    float roll;     // 翻滚角 (度)
} qma6100p_rawdata_t;
```

### 应用示例

#### 姿态监测
```c
void attitude_monitor_task(void *arg) {
    qma6100p_rawdata_t data;
    
    while (1) {
        // 读取数据
        qma6100p_read_rawdata(&data);
        
        // 检测倾斜
        if (abs(data.pitch) > 30 || abs(data.roll) > 30) {
            ESP_LOGW(TAG, "设备倾斜角度过大!");
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

#### 振动检测
```c
void vibration_detect_task(void *arg) {
    qma6100p_rawdata_t data;
    float threshold = 2.0;  // 振动阈值
    
    while (1) {
        qma6100p_read_rawdata(&data);
        
        // 计算加速度向量模
        float magnitude = sqrt(data.acc_x * data.acc_x + 
                              data.acc_y * data.acc_y + 
                              data.acc_z * data.acc_z);
        
        // 检测振动（去除重力加速度9.8）
        if (abs(magnitude - 9.8) > threshold) {
            ESP_LOGW(TAG, "检测到振动!");
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
```

### 硬件连接

#### I2C接线
```
QMA6100P    ESP32
--------    -------
VCC   →     3.3V
GND   →     GND
SDA   →     I2C_SDA (与音频Codec共用I2C总线)
SCL   →     I2C_SCL
```

**注意**: QMA6100P与ES8388音频Codec共用I2C总线，地址不冲突。

### 技术参数

- **测量范围**: ±2g / ±4g / ±8g (可配置)
- **分辨率**: 14位
- **输出速率**: 最高1000Hz
- **工作电压**: 1.8V - 3.6V
- **I2C地址**: 0x12 (7位地址)
- **功耗**: 
  - 正常模式: <200μA
  - 睡眠模式: <2μA

### 详细文档

更多信息请参考：
- `README_QMA6100P.md` - 完整功能说明和API参考
- `INTEGRATION_QMA6100P.md` - 集成步骤和配置说明

## 添加新传感器

### 目录结构建议
```
sensor/
├── README.md
├── SENSOR_NAME/
│   ├── sensor_name.c
│   ├── sensor_name.h
│   └── README.md
└── README_SENSOR_NAME.md
```

### 代码模板
```c
// sensor_name.h
#ifndef SENSOR_NAME_H
#define SENSOR_NAME_H

#include <esp_err.h>
#include <driver/i2c_master.h>

// 初始化传感器
esp_err_t sensor_name_init(i2c_master_bus_handle_t i2c_bus);

// 读取数据
esp_err_t sensor_name_read_data(sensor_data_t* data);

// 其他功能...

#endif // SENSOR_NAME_H
```

### 集成步骤
1. 在`sensor/`目录下创建传感器子目录
2. 实现驱动代码（.c/.h文件）
3. 编写README文档
4. 在主文件中包含头文件
5. 调用初始化和读取函数
6. 更新本README，添加新传感器说明

## 依赖关系

- **I2C驱动**: 所有I2C传感器共用
- **SPI驱动**: 用于SPI接口传感器（如果有）
- 无其他模块依赖，可独立使用

## 配置选项

在`config.h`中可以配置：
```cpp
// 启用/禁用QMA6100P
#define ENABLE_QMA6100P_FEATURE 1

// 传感器采样周期（ms）
#define SENSOR_SAMPLE_PERIOD 100

// I2C配置
#define SENSOR_I2C_SDA_PIN   6
#define SENSOR_I2C_SCL_PIN   7
```

## 注意事项

1. **I2C总线共享**:
   - 多个传感器可共用I2C总线
   - 确保地址不冲突
   - 注意上拉电阻（通常4.7kΩ）

2. **采样频率**:
   - 根据应用需求选择合适的采样频率
   - 过高频率增加CPU负担
   - 过低频率可能错过事件

3. **数据滤波**:
   - 传感器原始数据可能有噪声
   - 建议使用滑动平均或卡尔曼滤波
   - 对于加速度计，低通滤波可以减少抖动

4. **坐标系**:
   - 注意传感器安装方向
   - 可能需要坐标变换
   - 俯仰角和翻滚角定义要明确

5. **校准**:
   - 某些传感器需要校准（如磁力计）
   - 加速度计可能需要零点校准
   - 温度传感器可能需要温度补偿

## 应用场景

### 1. 运动检测
- 跌倒检测
- 手势识别
- 计步器

### 2. 姿态监控
- 设备倾斜检测
- 水平校准
- 稳定性监测

### 3. 振动分析
- 故障诊断
- 机械健康监测
- 冲击检测

### 4. 环境监测
- 温湿度记录
- 气压变化
- 空气质量

## 扩展功能

可以基于传感器模块实现：
- 数据融合（IMU融合）
- 姿态解算（四元数/欧拉角）
- 事件触发（阈值检测）
- 数据记录与回放
- 无线传输（WiFi/BLE）

## 常见问题

**Q: 传感器初始化失败怎么办？**
A: 检查：
- I2C接线是否正确
- 上拉电阻是否安装
- I2C地址是否正确
- 供电是否稳定

**Q: 数据读取不稳定？**
A: 建议：
- 添加数据滤波
- 检查采样频率
- 确认硬件连接可靠
- 查看传感器数据手册

**Q: 如何提高精度？**
A: 可以：
- 进行校准
- 使用更高精度的传感器
- 数据融合多个传感器
- 温度补偿

## 相关资源

- **QMA6100P数据手册**: 查看芯片厂商官网
- **ESP-IDF I2C驱动文档**: https://docs.espressif.com/projects/esp-idf/
- **传感器选型指南**: 根据应用需求选择合适的传感器

