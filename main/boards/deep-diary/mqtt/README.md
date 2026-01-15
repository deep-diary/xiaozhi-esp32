# Thumbler 不倒翁 MQTT 通信模块

本模块实现了 Thumbler 不倒翁设备与 Web 服务器的 MQTT 通信功能，用于设备状态上报和远程控制。

## 功能特性

- **Thumbler 协议支持**：完全符合 Thumbler 不倒翁产品协议规范
- **设备状态上报**：实时上报传感器数据、LED 状态、不倒翁工作模式等
- **远程控制**：接收并执行来自 Web 的控制指令（LED、摄像头、不倒翁模式等）
- **自动重连**：网络断开时自动重连
- **配置管理**：支持 NVS 存储配置信息

## 文件结构

```
mqtt/
├── user_mqtt_client.h/cpp      # MQTT 客户端类
├── user_mqtt_config.h/cpp      # 配置管理类
├── remote_control_handler.h/cpp # 远程控制指令处理器
├── device_info_collector.h/cpp  # 设备信息收集器
└── README.md                   # 本文档
```

## MQTT 主题结构

### 订阅主题（设备接收控制命令）
- **控制命令**：`Thumbler/{device_id}/cmd` (QoS=1)

### 发布主题（设备发送状态）
- **设备状态**：`Thumbler/{device_id}/status` (QoS=0)

其中 `{device_id}` 为设备唯一标识符，格式为：`ATK-DNESP32S3-{MAC地址}`

## 协议说明

### 1. 设备状态消息（设备 → Web）

设备定期发布状态信息到 `Thumbler/{device_id}/status` 主题，消息格式为 JSON：

```json
{
  "cur_cam_switch": true,
  "g_acc_x": 0.12,
  "g_acc_y": -0.05,
  "g_acc_z": 9.81,
  "g_acc_g": 9.82,
  "g_pitch": 2.5,
  "g_roll": -1.2,
  "cur_led_mode": 2,
  "cur_led_brightness": 128,
  "cur_led_low_brightness": 16,
  "cur_led_color_red": 0,
  "cur_led_color_green": 255,
  "cur_led_color_blue": 0,
  "cur_led_interval_ms": 500,
  "cur_led_scroll_length": 3,
  "cur_tumbler_mode": 1,
  "is_has_people": true,
  "power_percent": 85,
  "timestamp": 1704067200
}
```

#### 字段说明

| 字段名                   | 类型    | 说明                          | 单位/取值范围                                                         |
| ------------------------ | ------- | ----------------------------- | --------------------------------------------------------------------- |
| `cur_cam_switch`         | boolean | 摄像头开关状态                | true/false                                                            |
| `g_acc_x`                | float   | X 轴加速度                    | m/s²                                                                  |
| `g_acc_y`                | float   | Y 轴加速度                    | m/s²                                                                  |
| `g_acc_z`                | float   | Z 轴加速度                    | m/s²                                                                  |
| `g_acc_g`                | float   | 总加速度                      | m/s²                                                                  |
| `g_pitch`                | float   | 俯仰角                        | 度 (°)                                                                |
| `g_roll`                 | float   | 翻滚角                        | 度 (°)                                                                |
| `cur_led_mode`           | integer | 当前 LED 工作模式             | 0: 关闭, 1: 静态颜色, 2: 闪烁, 3: 呼吸灯, 4: 流水灯/滚动, 5: 系统状态 |
| `cur_led_brightness`     | integer | 当前 LED 默认亮度             | 0-255                                                                 |
| `cur_led_low_brightness` | integer | 当前 LED 低亮度               | 0-255                                                                 |
| `cur_led_color_red`      | integer | 当前 LED 颜色 - 红色分量      | 0-255                                                                 |
| `cur_led_color_green`    | integer | 当前 LED 颜色 - 绿色分量      | 0-255                                                                 |
| `cur_led_color_blue`     | integer | 当前 LED 颜色 - 蓝色分量      | 0-255                                                                 |
| `cur_led_interval_ms`    | integer | 当前 LED 动画间隔时间         | 毫秒                                                                  |
| `cur_led_scroll_length`  | integer | 当前 LED 滚动模式下的亮灯数量 | 1-最大 LED 数量                                                       |
| `cur_tumbler_mode`       | integer | 不倒翁工作模式                | 0: 静止, 1: 左右循环晃动, 2: 来回旋转, 3: 充电中                      |
| `is_has_people`          | boolean | 当前环境是否有人              | true/false                                                            |
| `power_percent`          | integer | 当前系统电量                  | 0-100 (%)                                                             |
| `timestamp`              | integer | 时间戳                        | Unix 时间戳（秒）                                                     |

### 2. 控制命令消息（Web → 设备）

Web 发布控制命令到 `Thumbler/{device_id}/cmd` 主题，消息格式为 JSON：

#### 基础控制示例

```json
{
  "tar_cam_switch": true,
  "tar_pitch": 10.0,
  "tar_roll": -5.0,
  "tar_tumbler_mode": 1,
  "timestamp": 1704067200
}
```

#### LED 控制示例

**静态颜色（模式 1）：**
```json
{
  "tar_led_mode": 1,
  "tar_led_brightness": 128,
  "tar_led_color_red": 255,
  "tar_led_color_green": 0,
  "tar_led_color_blue": 0,
  "timestamp": 1704067200
}
```

**闪烁（模式 2）：**
```json
{
  "tar_led_mode": 2,
  "tar_led_brightness": 128,
  "tar_led_color_red": 0,
  "tar_led_color_green": 255,
  "tar_led_color_blue": 0,
  "tar_led_interval_ms": 500,
  "timestamp": 1704067200
}
```

**呼吸灯（模式 3）：**
```json
{
  "tar_led_mode": 3,
  "tar_led_brightness": 128,
  "tar_led_color_low_red": 0,
  "tar_led_color_low_green": 0,
  "tar_led_color_low_blue": 0,
  "tar_led_color_red": 0,
  "tar_led_color_green": 255,
  "tar_led_color_blue": 0,
  "tar_led_interval_ms": 50,
  "timestamp": 1704067200
}
```

**流水灯/滚动（模式 4）：**
```json
{
  "tar_led_mode": 4,
  "tar_led_brightness": 128,
  "tar_led_color_low_red": 0,
  "tar_led_color_low_green": 0,
  "tar_led_color_low_blue": 0,
  "tar_led_color_red": 0,
  "tar_led_color_green": 0,
  "tar_led_color_blue": 255,
  "tar_led_scroll_length": 3,
  "tar_led_interval_ms": 100,
  "timestamp": 1704067200
}
```

#### 控制字段说明

**基础控制字段：**

| 字段名             | 类型    | 说明               | 单位/取值范围                                    |
| ------------------ | ------- | ------------------ | ------------------------------------------------ |
| `tar_cam_switch`   | boolean | 摄像头开关控制指令 | true: 开启, false: 关闭                          |
| `tar_pitch`        | float   | 目标俯仰角         | 度 (°)，范围待定                                 |
| `tar_roll`         | float   | 目标翻滚角         | 度 (°)，范围待定                                 |
| `tar_tumbler_mode` | integer | 目标不倒翁工作模式 | 0: 静止, 1: 左右循环晃动, 2: 来回旋转, 3: 充电中 |

**LED 控制字段：**

| 字段名                    | 类型    | 说明                               | 单位/取值范围                                                         |
| ------------------------- | ------- | ---------------------------------- | --------------------------------------------------------------------- |
| `tar_led_mode`            | integer | 目标 LED 工作模式                  | 0: 关闭, 1: 静态颜色, 2: 闪烁, 3: 呼吸灯, 4: 流水灯/滚动, 5: 系统状态 |
| `tar_led_brightness`      | integer | 目标 LED 默认亮度                  | 0-255                                                                 |
| `tar_led_low_brightness`  | integer | 目标 LED 低亮度（用于系统状态）    | 0-255                                                                 |
| `tar_led_color_red`       | integer | LED 颜色 - 红色分量                | 0-255                                                                 |
| `tar_led_color_green`     | integer | LED 颜色 - 绿色分量                | 0-255                                                                 |
| `tar_led_color_blue`      | integer | LED 颜色 - 蓝色分量                | 0-255                                                                 |
| `tar_led_color_low_red`   | integer | LED 低颜色 - 红色分量（呼吸/滚动） | 0-255                                                                 |
| `tar_led_color_low_green` | integer | LED 低颜色 - 绿色分量（呼吸/滚动） | 0-255                                                                 |
| `tar_led_color_low_blue`  | integer | LED 低颜色 - 蓝色分量（呼吸/滚动） | 0-255                                                                 |
| `tar_led_interval_ms`     | integer | LED 动画间隔时间（毫秒）           | > 0，建议范围：50-1000                                                |
| `tar_led_scroll_length`   | integer | LED 滚动模式下的亮灯数量           | 1-最大 LED 数量，仅用于滚动模式                                       |

## 使用方法

### 1. 基本初始化

在 `BoardExtensions` 类中，MQTT 客户端会在初始化时自动创建：

```cpp
// 在 BoardExtensions::InitializeUserMqtt() 中
user_mqtt_client_ = std::make_unique<UserMqttClient>();
remote_control_handler_ = std::make_unique<RemoteControlHandler>();
device_info_collector_ = std::make_unique<DeviceInfoCollector>();
```

### 2. 启动 MQTT 客户端

在 WiFi 连接成功后调用 `StartUserMqtt()`：

```cpp
void BoardExtensions::StartUserMqtt() {
    // 从 NVS 加载配置
    UserMqttConfig::LoadFromNvs();
    
    // 创建配置对象（使用 Thumbler 协议主题）
    UserMqttClientConfig config;
    config.broker_host = UserMqttConfig::GetBrokerHost();
    config.broker_port = UserMqttConfig::GetBrokerPort();
    config.client_id = UserMqttConfig::GetClientId();
    
    // Thumbler 协议主题格式
    std::string device_id = config.client_id; // 或从设备信息收集器获取
    config.status_topic = "Thumbler/" + device_id + "/status";
    config.control_topic = "Thumbler/" + device_id + "/cmd";
    
    // 初始化并连接
    if (user_mqtt_client_->Initialize(config)) {
        user_mqtt_client_->Connect();
    }
}
```

### 3. 定期发送状态

在主循环中定期发送设备状态：

```cpp
// 在主循环中
if (time_flags.mqtt_sensor_status) {
    DeviceStatus::SensorData sensor_status = device_info_collector_->CollectSensorStatus();
    user_mqtt_client_->SendThumblerStatus(sensor_status);
}
```

## LED 模式说明

| 模式值 | 名称       | 说明                           | 对应 CircularStrip 方法 |
| ------ | ---------- | ------------------------------ | ----------------------- |
| 0      | 关闭       | LED 全部关闭                   | Off()                   |
| 1      | 静态颜色   | 所有 LED 显示相同颜色          | SetAllColor()           |
| 2      | 闪烁       | LED 在指定颜色和关闭之间闪烁   | Blink()                 |
| 3      | 呼吸灯     | LED 在两种颜色之间平滑过渡     | Breathe()               |
| 4      | 流水灯/滚动 | LED 按顺序点亮，形成滚动效果   | Scroll()                |
| 5      | 系统状态   | LED 根据系统状态自动变化       | OnStateChanged()        |

## 不倒翁工作模式说明

| 模式值 | 名称         | 说明                     |
| ------ | ------------ | ------------------------ |
| 0      | 静止         | 不倒翁保持静止状态       |
| 1      | 左右循环晃动 | 不倒翁左右循环摆动       |
| 2      | 来回旋转     | 不倒翁来回旋转           |
| 3      | 充电中       | 设备正在充电，限制运动   |

## 配置参数

### 默认配置
- **Broker 地址**: `35.192.64.247`
- **端口**: `1883`
- **Keepalive**: `60秒`
- **SSL**: `关闭`

### 配置存储
配置信息存储在 NVS 的 `user_mqtt` 命名空间中：
- `broker_host` - MQTT 服务器地址
- `broker_port` - MQTT 服务器端口
- `client_id` - 客户端 ID（自动生成，格式：`ATK-DNESP32S3-{MAC地址}`）
- `username` - 用户名（可选）
- `password` - 密码（可选）
- `keepalive_interval` - 心跳间隔
- `use_ssl` - 是否使用 SSL

## 错误处理

### 连接错误
- 网络不可用时自动重连
- 最大重试次数：5次
- 重连间隔：30秒

### 消息错误
- JSON 解析失败时记录错误日志
- 未知命令类型时忽略该字段
- 设备组件不可用时记录警告

## 注意事项

1. **设备 ID 格式**：设备 ID 必须与 Web 端使用的格式一致，格式为 `ATK-DNESP32S3-{MAC地址}`
2. **主题格式**：必须严格按照 `Thumbler/{device_id}/status` 和 `Thumbler/{device_id}/cmd` 格式
3. **QoS 级别**：状态消息使用 QoS=0，控制命令使用 QoS=1
4. **时间戳**：所有消息必须包含 `timestamp` 字段（Unix 时间戳，秒）
5. **线程安全**：MQTT 回调在独立线程中执行，注意线程安全

## 调试和日志

启用相关日志标签：
- `UserMQTT` - MQTT 客户端日志
- `UserMqttConfig` - 配置管理日志
- `RemoteControl` - 远程控制日志
- `DeviceInfo` - 设备信息日志

## 版本历史

- **v2.0.0** - 更新为 Thumbler 协议，支持新的主题格式和消息结构
- **v1.0.0** - 初始版本，支持基本的 MQTT 通信和远程控制功能
