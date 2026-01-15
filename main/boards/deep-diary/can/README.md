# CAN通信模块 (CAN Communication Module)

## 功能概述

本模块提供了基于ESP32 TWAI（CAN2.0）的通信功能，包括：
- CAN总线初始化与配置
- 标准帧和扩展帧支持
- 消息发送与接收
- 过滤器配置
- 错误处理

## 文件说明

### 核心文件

- **ESP32-TWAI-CAN.cpp/hpp**: CAN总线驱动封装
  - 基于ESP-IDF TWAI驱动
  - 简化的API接口
  - 阻塞和非阻塞模式

## 主要功能

### 1. CAN总线初始化
```cpp
// 初始化CAN总线（波特率, TX引脚, RX引脚, TX队列, RX队列）
bool result = ESP32Can.begin(
    ESP32Can.convertSpeed(1000),  // 1000kbps
    CAN_TX_GPIO,                  // TX引脚
    CAN_RX_GPIO,                  // RX引脚
    10,                           // TX队列大小
    10                            // RX队列大小
);

if (result) {
    ESP_LOGI(TAG, "CAN总线初始化成功");
} else {
    ESP_LOGE(TAG, "CAN总线初始化失败");
}
```

### 2. 发送CAN帧
```cpp
// 创建CAN帧
CanFrame txFrame;
txFrame.identifier = 0x123;      // CAN ID
txFrame.extd = 0;                // 标准帧
txFrame.data_length_code = 8;    // 数据长度

// 填充数据
txFrame.data[0] = 0x01;
txFrame.data[1] = 0x02;
// ... 填充其他数据

// 发送帧（阻塞模式，超时1000ms）
if (ESP32Can.writeFrame(txFrame, 1000)) {
    ESP_LOGI(TAG, "发送成功");
} else {
    ESP_LOGE(TAG, "发送失败");
}
```

### 3. 接收CAN帧
```cpp
CanFrame rxFrame;

// 接收帧（阻塞模式，超时1000ms）
if (ESP32Can.readFrame(rxFrame, 1000)) {
    ESP_LOGI(TAG, "接收到CAN帧: ID=0x%03lX, 长度=%d", 
             rxFrame.identifier, rxFrame.data_length_code);
    
    // 处理数据
    for (int i = 0; i < rxFrame.data_length_code; i++) {
        ESP_LOGI(TAG, "数据[%d] = 0x%02X", i, rxFrame.data[i]);
    }
}
```

### 4. 扩展帧支持
```cpp
// 创建扩展帧
CanFrame txFrame;
txFrame.identifier = 0x12345678;  // 29位扩展ID
txFrame.extd = 1;                 // 扩展帧标志
txFrame.data_length_code = 8;

// 发送和接收方式与标准帧相同
ESP32Can.writeFrame(txFrame, 1000);
```

## CAN帧结构

### CanFrame结构体
```cpp
typedef struct {
    uint32_t identifier;      // CAN ID（标准帧11位，扩展帧29位）
    uint8_t extd;             // 扩展帧标志（0=标准帧, 1=扩展帧）
    uint8_t rtr;              // 远程帧标志
    uint8_t data_length_code; // 数据长度（0-8）
    uint8_t data[8];          // 数据缓冲区
} CanFrame;
```

## 波特率配置

### 常用波特率
```cpp
// 支持的波特率（kbps）
ESP32Can.convertSpeed(25);    // 25kbps
ESP32Can.convertSpeed(50);    // 50kbps
ESP32Can.convertSpeed(100);   // 100kbps
ESP32Can.convertSpeed(125);   // 125kbps
ESP32Can.convertSpeed(250);   // 250kbps
ESP32Can.convertSpeed(500);   // 500kbps
ESP32Can.convertSpeed(800);   // 800kbps
ESP32Can.convertSpeed(1000);  // 1000kbps (1Mbps)
```

## 使用示例

### 简单收发示例
```cpp
// 初始化
ESP32Can.begin(ESP32Can.convertSpeed(1000), 4, 5, 10, 10);

// 发送任务
void can_tx_task(void *arg) {
    CanFrame frame;
    frame.identifier = 0x100;
    frame.extd = 0;
    frame.data_length_code = 2;
    
    while (1) {
        frame.data[0]++;
        frame.data[1] = frame.data[0] * 2;
        
        ESP32Can.writeFrame(frame, 1000);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 接收任务
void can_rx_task(void *arg) {
    CanFrame frame;
    
    while (1) {
        if (ESP32Can.readFrame(frame, 1000)) {
            // 处理接收到的帧
            processCanFrame(frame);
        }
    }
}
```

### 电机控制示例
```cpp
// 发送电机使能命令
void enableMotor(uint8_t motor_id) {
    CanFrame frame;
    frame.identifier = motor_id;
    frame.extd = 0;
    frame.data_length_code = 8;
    frame.data[0] = 0xFF;  // 命令字节
    frame.data[1] = 0xFF;
    frame.data[2] = 0xFF;
    frame.data[3] = 0xFF;
    frame.data[4] = 0xFF;
    frame.data[5] = 0xFF;
    frame.data[6] = 0xFF;
    frame.data[7] = 0xFC;  // 使能命令
    
    ESP32Can.writeFrame(frame, 1000);
}

// 接收电机反馈
void processMotorFeedback(const CanFrame& frame) {
    uint8_t motor_id = frame.identifier;
    int16_t angle = (frame.data[6] << 8) | frame.data[7];
    
    ESP_LOGI(TAG, "电机%d当前角度: %d", motor_id, angle);
}
```

## CAN ID分配

### 建议的ID分配方案
```cpp
// 电机控制（0x001 - 0x0FF）
#define CAN_ID_MOTOR_BASE   0x001
#define CAN_ID_MOTOR_1      0x001
#define CAN_ID_MOTOR_2      0x002
// ... 更多电机

// 系统指令（0x100 - 0x1FF）
#define CAN_CMD_SERVO_CONTROL  0x101
#define CAN_CMD_SYSTEM_CTRL    0x102

// 传感器数据（0x200 - 0x2FF）
#define CAN_SENSOR_IMU         0x201
#define CAN_SENSOR_PRESSURE    0x202

// 调试信息（0x700 - 0x7FF）
#define CAN_DEBUG_INFO         0x701
```

## 硬件连接

### CAN收发器接线
```
ESP32       CAN收发器(TJA1050/SN65HVD230)
------      ---------------------------
CAN_TX →    TXD
CAN_RX →    RXD
GND    →    GND
5V     →    VCC

CAN收发器   CAN总线
---------   -------
CANH    →   CANH
CANL    →   CANL
```

### 终端电阻
- CAN总线两端需要120Ω终端电阻
- 短距离通信（<1米）可省略

### GPIO配置
默认配置（可在config.h中修改）：
```cpp
#define CAN_TX_GPIO  4
#define CAN_RX_GPIO  5
```

## 性能参数

- **最大波特率**: 1Mbps
- **最大节点数**: 110个（理论值）
- **最大总线长度**: 
  - 1Mbps: 40米
  - 500kbps: 100米
  - 125kbps: 500米
- **标准帧ID范围**: 0x000 - 0x7FF (11位)
- **扩展帧ID范围**: 0x00000000 - 0x1FFFFFFF (29位)

## 依赖关系

- **ESP-IDF TWAI驱动**: CAN底层硬件驱动
- 无其他模块依赖，可独立使用

## 被使用的模块

本模块被以下模块使用：
- **电机模块** (`../motor/`): 电机控制指令收发
- **云台模块** (`../gimbal/`): 云台控制指令

## 注意事项

1. **硬件要求**:
   - 必须使用CAN收发器（如TJA1050、SN65HVD230）
   - ESP32的GPIO不能直接连接CAN总线
   - 总线两端需要120Ω终端电阻

2. **波特率匹配**:
   - 总线上所有节点必须使用相同波特率
   - 推荐使用标准波特率（125k, 250k, 500k, 1M）

3. **错误处理**:
   - 读写超时不代表硬件故障
   - 注意检查返回值
   - 总线错误会自动恢复

4. **队列大小**:
   - TX/RX队列大小根据实际需求调整
   - 队列过小可能导致消息丢失
   - 队列过大占用更多内存

5. **优先级**:
   - CAN ID越小优先级越高
   - 重要消息应使用较小的ID

## 调试技巧

### 使能调试日志
```cpp
esp_log_level_set("ESP32-CAN", ESP_LOG_DEBUG);
```

### 常见问题排查
1. **初始化失败**: 检查引脚配置和收发器连接
2. **发送失败**: 检查总线是否连接、是否有其他节点
3. **接收不到数据**: 检查波特率是否匹配、过滤器配置
4. **总线错误**: 检查终端电阻、总线拓扑、连接质量

### 监控工具
- 使用CAN分析仪监控总线流量
- 使用示波器检查信号质量
- 使用逻辑分析仪解码CAN协议

## 扩展功能

可以基于本模块实现：
- 消息过滤器（只接收特定ID）
- 错误统计与监控
- 总线负载分析
- 自动重传机制
- CAN FD支持（需硬件支持）

## 相关标准

- **CAN 2.0A**: 标准帧（11位ID）
- **CAN 2.0B**: 扩展帧（29位ID）
- **ISO 11898**: CAN物理层和数据链路层标准

