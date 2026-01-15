# ATK-DNESP32S3 板级代码架构说明

## 📋 目录结构

```
atk-dnesp32s3/
├── config.h                    # 配置中心 - 所有功能开关和GPIO定义
├── config.json                 # 板级配置文件
├── atk_dnesp32s3.cc           # 主板文件（最小改动，保持与开源同步）
├── board_extensions.h/cc      # 板级扩展（所有自定义功能）
│
├── motor/                      # 电机控制模块
│   ├── protocol_motor.h/cc    # 电机协议层
│   └── deep_motor.h/cc        # 深度电机驱动
│
├── arm/                        # 机械臂控制模块
│   ├── deep_arm.h/cc          # 机械臂控制
│   └── deep_arm_control.h/cc  # 机械臂MCP控制接口
│
├── gimbal/                     # 云台控制模块
│   ├── Gimbal.h/c             # 云台驱动（C风格）
│   └── gimbal_control.h/cc    # 云台MCP控制接口
│
├── servo/                      # 舵机驱动模块
│   └── Servo.h/c              # 舵机底层驱动（C风格）
│
├── led/                        # LED控制模块
│   └── led_control.h/cc       # WS2812灯带MCP控制接口
│
├── can/                        # CAN总线模块
│   └── ESP32-TWAI-CAN.hpp     # ESP32 CAN驱动
│
├── sensor/                     # 传感器模块
│   └── QMA6100P/              # 三轴加速度计
│       ├── qma6100p.h/c       # QMA6100P驱动
│       └── README_QMA6100P.md # 使用说明
│
├── streaming/                  # 流媒体模块
│   ├── mjpeg_server.h/cc      # MJPEG HTTP服务器（可选）
│   └── RTSP_MIGRATION_PLAN.md # RTSP迁移计划
│
└── doc/                        # 文档目录
    ├── README.md              # 板级使用说明
    ├── FEATURE_FLAGS.md       # 功能开关详解
    └── ...                    # 其他文档
```

## 🏗️ 核心架构设计

### 1. 双层架构模式

```
┌─────────────────────────────────────────────────────┐
│          应用层 (Application)                        │
│  - 语音识别   - 对话管理   - 状态机                  │
└─────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────┐
│          板级抽象层 (Board Interface)                │
│  - GetDisplay()  - GetCamera()  - GetAudioCodec()   │
└─────────────────────────────────────────────────────┘
                        ↓
┌──────────────────┬──────────────────────────────────┐
│  主板文件        │  板级扩展 (BoardExtensions)       │
│ atk_dnesp32s3.cc │  - 封装所有自定义功能              │
│ - 最小改动       │  - 独立维护                        │
│ - 易于升级       │  - 模块化设计                      │
└──────────────────┴──────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────┐
│          功能模块层 (Feature Modules)                │
│  Motor | Arm | Gimbal | LED | Sensor | Camera ...  │
└─────────────────────────────────────────────────────┘
                        ↓
┌─────────────────────────────────────────────────────┐
│          硬件抽象层 (HAL)                            │
│  I2C | SPI | CAN | PWM | GPIO ...                   │
└─────────────────────────────────────────────────────┘
```

### 2. 主板文件 (atk_dnesp32s3.cc)

**职责**：
- 实现 Board 接口
- 初始化基础硬件（I2C、SPI、显示、音频）
- 创建并管理 BoardExtensions 实例
- **保持最小改动，便于合并开源更新**

**关键代码**：
```cpp
class atk_dnesp32s3 : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    LcdDisplay* display_;
    BoardExtensions* extensions_;  // ← 扩展功能入口

public:
    atk_dnesp32s3() {
        // 1. 初始化基础硬件
        InitializeI2c();
        InitializeSpi();
        display_ = InitializeLcdDisplay();
        
        // 2. 创建扩展对象（传入必要资源）
        extensions_ = new BoardExtensions(i2c_bus_, display_);
    }
    
    // 3. 实现Board接口
    virtual Display* GetDisplay() override { return display_; }
    virtual Camera* GetCamera() override { 
        return extensions_ ? extensions_->GetCamera() : nullptr;
    }
};
```

### 3. 板级扩展 (BoardExtensions)

**设计目的**：
- 将所有自定义功能封装在独立的类中
- 与开源主板文件分离，便于维护和升级
- 统一管理各个功能模块的生命周期

**核心结构**：
```cpp
class BoardExtensions {
public:
    BoardExtensions(i2c_master_bus_handle_t i2c_bus, LcdDisplay* display);
    ~BoardExtensions();
    
    // 初始化函数（按功能分组）
    void InitializeXL9555();          // I/O扩展
    Camera* InitializeCamera();        // 摄像头
    void InitializeGimbal();          // 云台
    void InitializeWs2812();          // LED灯带
    void InitializeCan();             // CAN总线
    void InitializeQMA6100P();        // 加速度计
    void InitializeControls();        // MCP控制接口
    void StartUserMainLoop();         // 用户任务
    
    // 访问器
    Camera* GetCamera() { return camera_; }
    Gimbal_t* GetGimbal() { return gimbal_; }
    DeepMotor* GetDeepMotor() { return deep_motor_; }
    // ...更多访问器
    
private:
    // 硬件资源
    i2c_master_bus_handle_t i2c_bus_;
    LcdDisplay* display_;
    
    // 外设对象
    XL9555* xl9555_;                  // I/O扩展
    Camera* camera_;                  // 摄像头
    Gimbal_t* gimbal_;                // 云台
    CircularStrip* led_strip_;        // LED灯带
    
    // 电机系统
    DeepMotor* deep_motor_;           // 电机管理器
    DeepArm* deep_arm_;               // 机械臂
    
    // 控制接口
    LedStripControl* led_control_;    // LED控制
    DeepMotorControl* motor_control_; // 电机控制
    
    // 任务句柄
    TaskHandle_t user_main_loop_task_handle_;
    TaskHandle_t can_receive_task_handle_;
};
```

**初始化流程**：
```cpp
BoardExtensions::BoardExtensions(...) {
    // 1. I/O扩展（必须最先初始化，其他设备依赖它）
    InitializeXL9555();
    
    // 2. 摄像头（需要XL9555控制电源和复位）
    camera_ = InitializeCamera();
    
    // 3. 云台舵机
    InitializeGimbal();
    
    // 4. LED和CAN（根据配置选择性初始化）
    InitializeWs2812();
    InitializeCan();
    
    // 5. 传感器
    InitializeQMA6100P();
    
    // 6. MCP控制接口
    InitializeControls();
    
    // 7. 启动用户任务
    StartUserMainLoop();
}
```

### 4. 配置管理 (config.h)

**集中式配置**：
```cpp
// GPIO引脚定义
#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_3
#define LCD_SCLK_PIN GPIO_NUM_12
#define CAM_PIN_D0 GPIO_NUM_4
#define SERVO_PAN_GPIO GPIO_NUM_19
#define CAN_TX_GPIO GPIO_NUM_5
#define WS2812_STRIP_GPIO GPIO_NUM_7

// 功能开关（条件编译）
#define ENABLE_CAMERA_FEATURE 1        // 相机
#define ENABLE_CAN_FEATURE 0           // CAN总线
#define ENABLE_LED_STRIP_FEATURE 0     // LED灯带
#define ENABLE_QMA6100P_FEATURE 1      // 加速度计
#define ENABLE_MJPEG_FEATURE 1         // MJPEG服务器

// 自动冲突处理
#if ENABLE_CAMERA_FEATURE
    #undef ENABLE_CAN_FEATURE
    #define ENABLE_CAN_FEATURE 0
#endif
```

**优势**：
- ✅ 统一管理配置
- ✅ 条件编译减少代码体积
- ✅ 自动处理GPIO冲突
- ✅ 易于切换功能组合

### 5. 模块化设计

每个功能模块遵循统一的设计模式：

#### 示例：电机控制模块

```
motor/
├── protocol_motor.h/cc        # 协议层（CAN通信）
├── deep_motor.h/cc           # 驱动层（电机控制）
└── deep_motor_control.h/cc   # 应用层（MCP接口）
```

**层次关系**：
```
MCP Server (外部调用)
    ↓
DeepMotorControl (应用层)
    ↓
DeepMotor (驱动层)
    ↓
ProtocolMotor (协议层)
    ↓
CAN Bus (硬件)
```

## 🔄 数据流示例

### 示例1：语音控制LED灯带

```
用户语音 "打开红灯"
    ↓
Application (语音识别)
    ↓
MCP Server (调用工具)
    ↓
LedStripControl::SetColor("red")
    ↓
CircularStrip::SetColor(rgb)
    ↓
WS2812驱动
    ↓
LED灯带显示红色
```

### 示例2：加速度计数据显示

```
QMA6100P硬件
    ↓
qma6100p_read_rawdata() (驱动层)
    ↓
user_main_loop_task (任务层，500ms周期)
    ↓
display_->SetChatMessage() (显示层)
    ↓
屏幕显示加速度数据
```

### 示例3：拍照请求

```
MCP客户端 "拍照"
    ↓
MCP Server
    ↓
Application::TakePhoto()
    ↓
Board::GetCamera()
    ↓
BoardExtensions::GetCamera()
    ↓
Esp32Camera::Capture()
    ↓
返回图像数据
```

## 🎯 设计原则

### 1. 分层解耦
- **应用层**不直接操作硬件
- **板级层**提供统一接口
- **驱动层**封装硬件细节

### 2. 单一职责
- 每个模块只负责一个功能
- 每个类只做一件事
- 便于测试和维护

### 3. 开闭原则
- 对扩展开放：添加新功能不影响现有代码
- 对修改关闭：主板文件保持最小改动

### 4. 依赖注入
```cpp
// BoardExtensions接收外部依赖
BoardExtensions(i2c_master_bus_handle_t i2c_bus,  // ← 注入
                LcdDisplay* display)               // ← 注入
```

### 5. 资源管理
- 构造函数中初始化资源
- 析构函数中释放资源
- 使用RAII模式（智能指针）

## 🔌 硬件资源管理

### I2C总线
```
I2C Bus (共享总线)
├── ES8388 (音频编解码器) - 地址 0x10
├── XL9555 (I/O扩展)       - 地址 0x20
├── QMA6100P (加速度计)    - 地址 0x12
└── OV2640 (摄像头SCCB)    - 地址 0x30
```

### SPI总线
```
SPI Bus
└── LCD Display (ST7789)
```

### GPIO分配
```
冲突组1（相机 vs CAN/LED）：
- GPIO4: CAM_D0 / CAN_RX
- GPIO5: CAM_D1 / CAN_TX  
- GPIO7: CAM_D3 / WS2812

独立功能：
- GPIO19: 云台水平舵机 (PWM)
- GPIO20: 云台俯仰舵机 (PWM)
- GPIO0: Boot按钮
```

## 📊 任务架构

```
┌─────────────────────────────────────────────┐
│  main_task (主事件循环)                      │
│  - 处理应用事件                              │
│  - 协议消息处理                              │
└─────────────────────────────────────────────┘

┌─────────────────────────────────────────────┐
│  audio_task (音频处理)                       │
│  - 录音/播放                                 │
│  - 编解码                                    │
└─────────────────────────────────────────────┘

┌─────────────────────────────────────────────┐
│  user_main_loop_task (用户主循环)            │
│  - 加速度计数据读取                          │
│  - 显示更新                                  │
│  - 周期：10ms，采样：500ms                   │
└─────────────────────────────────────────────┘

┌─────────────────────────────────────────────┐
│  can_receive_task (CAN接收)                  │
│  - 电机反馈处理                              │
│  - 云台CAN指令                               │
│  - 仅在ENABLE_CAN_FEATURE时创建              │
└─────────────────────────────────────────────┘

┌─────────────────────────────────────────────┐
│  mjpeg_task (MJPEG服务器)                    │
│  - HTTP流媒体                                │
│  - 仅在ENABLE_MJPEG_FEATURE时创建            │
└─────────────────────────────────────────────┘
```

## 🛠️ 扩展新功能的步骤

### 场景：添加温湿度传感器

**Step 1**: 创建模块目录
```bash
mkdir sensor/DHT22
touch sensor/DHT22/dht22.h
touch sensor/DHT22/dht22.c
touch sensor/DHT22/README.md
```

**Step 2**: 在 `config.h` 添加配置
```cpp
#define ENABLE_DHT22_FEATURE 1
#define DHT22_GPIO GPIO_NUM_8
```

**Step 3**: 在 `board_extensions.h` 添加声明
```cpp
class BoardExtensions {
    // ...
    void InitializeDHT22();
    bool IsDHT22Initialized() const { return dht22_initialized_; }
    
private:
    bool dht22_initialized_;
};
```

**Step 4**: 在 `board_extensions.cc` 实现
```cpp
void BoardExtensions::InitializeDHT22() {
#if ENABLE_DHT22_FEATURE
    ESP_LOGI(TAG, "初始化DHT22...");
    dht22_init(DHT22_GPIO);
    dht22_initialized_ = true;
#endif
}

// 在构造函数中调用
BoardExtensions::BoardExtensions(...) {
    // ...
    InitializeDHT22();
}
```

**Step 5**: 在用户任务中读取数据
```cpp
void BoardExtensions::user_main_loop_task(...) {
    while (1) {
        if (ext->dht22_initialized_) {
            float temp, humidity;
            dht22_read(&temp, &humidity);
            // 处理数据
        }
    }
}
```

## 📚 关键文档索引

| 文档 | 说明 |
|------|------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | 本文档 - 架构说明 |
| [FEATURE_FLAGS.md](FEATURE_FLAGS.md) | 功能开关详解 |
| [REFACTORING_SUMMARY_V3.md](REFACTORING_SUMMARY_V3.md) | 重构总结 |
| [STREAMING_MIGRATION_STATUS.md](STREAMING_MIGRATION_STATUS.md) | 流媒体迁移 |
| [motor/README.md](motor/README.md) | 电机控制说明 |
| [sensor/README_QMA6100P.md](sensor/README_QMA6100P.md) | 加速度计说明 |

## 🎓 最佳实践

### ✅ DO
- 新功能放在 `board_extensions` 中
- 使用宏定义控制可选功能
- 每个模块提供 README
- 初始化时检查依赖关系
- 使用 RAII 管理资源

### ❌ DON'T
- 直接修改 `atk_dnesp32s3.cc` 添加功能
- 在函数内部频繁使用宏
- 硬编码GPIO和配置
- 忽略错误处理
- 循环依赖

## 🔍 调试技巧

### 1. 查看初始化日志
```
I (xxx) board_ext: 板级扩展功能初始化开始...
I (xxx) board_ext: 初始化XL9555 I/O扩展...
I (xxx) board_ext: 初始化相机功能...
I (xxx) board_ext: 初始化QMA6100P加速度计...
```

### 2. 检查功能是否启用
```bash
# 搜索宏定义
grep "ENABLE_.*_FEATURE" config.h

# 搜索初始化日志
grep "初始化" monitor.log
```

### 3. 内存监控
```cpp
ESP_LOGI(TAG, "Free heap: %u", esp_get_free_heap_size());
ESP_LOGI(TAG, "Min free: %u", esp_get_minimum_free_heap_size());
```

---

**维护者**: DeepDiary 团队  
**最后更新**: 2025-10-22  
**版本**: V3.0

