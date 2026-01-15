# QMA6100P 加速度计集成说明

## 📌 概述

本文档说明如何在 ATK-DNESP32S3 开发板上使用 QMA6100P 三轴加速度计传感器。该功能已经完全集成到项目中，包括自动采集和实时显示加速度数据。

## 🔧 硬件连接

QMA6100P 通过 I2C 总线与 ESP32-S3 通信：

| QMA6100P 引脚 | ESP32-S3 引脚 | 说明 |
|--------------|--------------|------|
| SDA | GPIO41 | I2C 数据线 |
| SCL | GPIO42 | I2C 时钟线 |
| VCC | 3.3V | 电源正极 |
| GND | GND | 电源地 |

**注意**：I2C 总线与 ES8388 音频编解码器共享，无需额外配置。

## ⚙️ 功能配置

### 启用/禁用加速度计功能

在 `atk_dnesp32s3.cc` 文件的第 36 行，可以控制加速度计功能的启用：

```cpp
// 是否启用QMA6100P加速度计
#define ENABLE_QMA6100P_FEATURE 1  // 1:启用, 0:禁用
```

### 功能特性

✅ **自动初始化**：在系统启动时自动检测并初始化 QMA6100P
✅ **实时采集**：每 200ms 采集一次三轴加速度数据
✅ **屏幕显示**：实时在 LCD 屏幕上显示加速度和姿态角度
✅ **日志输出**：通过串口实时输出加速度数据，方便调试
✅ **容错处理**：如果传感器未连接，系统仍能正常运行

## 📊 显示内容

系统会在屏幕上实时显示以下数据：

```
🔄 加速度计数据:
ACC_X: 0.15 m/s²    // X轴加速度
ACC_Y: 0.23 m/s²    // Y轴加速度
ACC_Z: 9.81 m/s²    // Z轴加速度
俯仰角: 1.5°        // Pitch (围绕X轴旋转)
翻滚角: -0.8°       // Roll (围绕Z轴旋转)
```

## 🔍 技术细节

### 初始化流程

1. **I2C 总线复用**：加速度计使用已初始化的 I2C 总线（与音频编解码器共享）
2. **设备检测**：尝试读取 QMA6100P 的芯片 ID (0x90)
3. **配置传感器**：
   - 测量范围：±8G
   - 采样频率：100Hz
   - 工作模式：主动模式

### 用户主循环任务

系统创建了一个独立的 FreeRTOS 任务来处理周期性的业务逻辑：

- **任务名称**：`user_main_loop`
- **任务优先级**：4 (中等优先级，介于系统任务和实时任务之间)
- **栈大小**：8192 字节（8KB，预留足够空间用于扩展其他功能）
- **更新频率**：10ms 基础周期，加速度计每 200ms 更新一次

### 数据计算

传感器提供以下计算值：

1. **三轴加速度** (ACC_X, ACC_Y, ACC_Z)：单位 m/s²
2. **俯仰角** (Pitch)：围绕 X 轴旋转的角度，单位°
3. **翻滚角** (Roll)：围绕 Z 轴旋转的角度，单位°

计算公式：
```c
Pitch = -atan2(acc_x, acc_z) * 180/π
Roll = asin(acc_y / √(acc_x² + acc_y² + acc_z²)) * 180/π
```

## 🚀 代码结构

### 关键文件

```
main/boards/atk-dnesp32s3/
├── QMA6100P/
│   ├── qma6100p.h          # 加速度计头文件
│   └── qma6100p.c          # 加速度计驱动实现
├── atk_dnesp32s3.cc        # 主板支持包（包含集成代码）
└── README_QMA6100P.md      # 本文档
```

### 核心函数

| 函数名 | 功能 | 位置 |
|--------|------|------|
| `qma6100p_init()` | 初始化 QMA6100P | qma6100p.c |
| `qma6100p_read_rawdata()` | 读取原始数据 | qma6100p.c |
| `InitializeQMA6100P()` | 集成到板级初始化 | atk_dnesp32s3.cc |
| `user_main_loop_task()` | 用户主循环任务 | atk_dnesp32s3.cc |

## 🛠️ 自定义开发

### 修改更新频率

在 `user_main_loop_task()` 中修改计数器阈值：

```cpp
if (update_counter >= 20 && board->qma6100p_initialized_) {  
    // 20 * 10ms = 200ms
    // 修改20为其他值可改变更新频率
    // 例如：10 = 100ms, 50 = 500ms
```

### 修改显示格式

在 `user_main_loop_task()` 中修改 `snprintf` 格式字符串：

```cpp
snprintf(msg_buffer, sizeof(msg_buffer),
         "🔄 加速度计数据:\n"
         "ACC_X: %.2f m/s²\n"    // 修改显示格式
         "ACC_Y: %.2f m/s²\n"
         "ACC_Z: %.2f m/s²\n"
         "俯仰角: %.1f°\n"
         "翻滚角: %.1f°",
         accel_data.acc_x,
         accel_data.acc_y,
         accel_data.acc_z,
         accel_data.pitch,
         accel_data.roll);
```

### 添加其他周期性任务

在 `user_main_loop_task()` 的主循环中添加你的代码：

```cpp
while (1) {  // 用户主循环
    vTaskDelay(pdMS_TO_TICKS(10));
    update_counter++;
    
    // ========== 加速度计数据采集和显示 ==========
    if (update_counter >= 20 && board->qma6100p_initialized_) {
        update_counter = 0;
        qma6100p_read_rawdata(&accel_data);
        // ... 显示到屏幕
    }
    
    // ========== 在这里添加其他周期性任务 ==========
    // 例如：
    // - 其他传感器读取（温度、湿度、距离等）
    // - 状态检查和监控
    // - 定时操作和任务调度
    // - 数据处理和上报
    // - 姿态识别、跌倒检测等
}
```

## 📝 日志输出

系统会在串口输出详细的日志信息：

```
I (xxx) atk_dnesp32s3: 初始化QMA6100P加速度计...
I (xxx) qma6100p: QMA6100P initialized successfully!
I (xxx) atk_dnesp32s3: QMA6100P加速度计初始化成功!
I (xxx) atk_dnesp32s3: 用户主循环任务创建成功!
I (xxx) atk_dnesp32s3: 用户主循环任务启动
I (xxx) atk_dnesp32s3: ACC[0.15, 0.23, 9.81] Pitch:1.5° Roll:-0.8°
```

## ⚠️ 故障排除

### 问题1：传感器初始化失败

**现象**：
```
W (xxx) atk_dnesp32s3: QMA6100P加速度计初始化失败，可能未连接传感器
```

**解决方案**：
1. 检查硬件连接（SDA、SCL、VCC、GND）
2. 确认传感器地址是否为 0x12
3. 使用万用表测量传感器电源是否为 3.3V

### 问题2：数据读取异常

**现象**：加速度值全为 0 或异常大

**解决方案**：
1. 检查 I2C 总线是否有冲突
2. 尝试降低 I2C 速率（在 `qma6100p_init()` 中修改 `scl_speed_hz`）
3. 重新上电复位

### 问题3：主循环任务未启动

**现象**：屏幕无显示，日志无输出

**解决方案**：
1. 检查任务创建是否成功（查看日志中的"用户主循环任务创建成功"）
2. 确认栈空间是否足够（当前8KB应该足够）
3. 如果添加了其他功能导致栈溢出，可以增加栈大小

## 🔗 参考资料

- [QMA6100P 数据手册](https://github.com/QST-Corporation/QMA6100P)
- [ESP-IDF I2C Master Driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/i2c.html)
- [ATK-DNESP32S3 开发板原理图](正点原子官网)

## 📄 许可证

本驱动代码基于正点原子团队的示例代码修改，保留原始版权信息。

---

**作者**：DeepDiary 开发团队  
**日期**：2025-01-21  
**版本**：v1.0

