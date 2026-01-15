# 电机软件版本查询问题修复总结

## 问题分析

### 问题1：活跃电机ID警告
**错误信息**：
```
W (35576) DeepMotorLedState: 活跃电机ID 255 超出范围
```

**根本原因**：
- `getActiveMotorId()`返回`int8_t`类型（-1表示无活跃电机）
- 但被赋值给`uint8_t`类型变量
- 当返回-1时，`uint8_t`将其解释为255
- 导致数组越界检查失败

### 问题2：软件版本号为空
**错误信息**：
```
I (51067) DeepMotor: 电机ID 1 软件版本: 
I (51067) DeepMotorControl: 获取电机ID 1 软件版本成功: 
```

**根本原因**：
- 版本号只在`MOTOR_CMD_VERSION`命令类型时才会解析
- 电机初始化时发送的是`MOTOR_CMD_FEEDBACK`命令
- 没有主动发送版本查询命令
- 版本号字段从未被填充

## 修复方案

### 修复1：活跃电机ID类型问题

**修复前**：
```cpp
uint8_t active_motor_id = deep_motor_->getActiveMotorId();
if (active_motor_id == 0) {
    // 检查逻辑
}
```

**修复后**：
```cpp
int8_t active_motor_id = deep_motor_->getActiveMotorId();
if (active_motor_id <= 0) {
    // 检查逻辑
}
```

**修复内容**：
- 使用正确的`int8_t`类型接收返回值
- 检查条件改为`<= 0`，正确处理-1值

### 修复2：添加版本查询功能

#### 2.1 添加版本查询方法
在`MotorProtocol`类中添加：
```cpp
/**
 * @brief 获取电机软件版本号
 * @param motor_id 电机ID
 * @return true 成功, false 失败
 */
static bool getMotorVersion(uint8_t motor_id);
```

#### 2.2 实现版本查询方法
```cpp
bool MotorProtocol::getMotorVersion(uint8_t motor_id) {
    ESP_LOGI(TAG, "获取电机%d软件版本号", motor_id);
    
    CanFrame frame;
    memset(&frame, 0, sizeof(frame));

    frame.identifier = buildCanId(motor_id, MOTOR_CMD_VERSION);
    frame.extd = 1;
    frame.rtr = 0;
    frame.ss = 0;
    frame.self = 0;
    frame.dlc_non_comp = 0;
    frame.data_length_code = 8;

    // 清空数据字段
    memset(frame.data, 0, 8);

    return sendCanFrame(frame);
}
```

#### 2.3 修改MCP工具逻辑
```cpp
// 先发送版本查询命令
if (!MotorProtocol::getMotorVersion(motor_id)) {
    return std::string("发送版本查询命令失败");
}

// 等待电机响应
vTaskDelay(pdMS_TO_TICKS(100));

// 获取版本号
char version[16];
if (deep_motor_->getMotorSoftwareVersion(motor_id, version, sizeof(version))) {
    return std::string("电机ID " + std::to_string(motor_id) + " 软件版本: " + std::string(version));
}
```

## 修复后的工作流程

### 版本查询流程
1. **发送查询命令**：`MotorProtocol::getMotorVersion(motor_id)`
2. **等待响应**：`vTaskDelay(pdMS_TO_TICKS(100))`
3. **解析响应**：`parseMotorData()`中的`MOTOR_CMD_VERSION`分支
4. **获取版本**：`getMotorSoftwareVersion()`从`motor_statuses_[index].version`读取
5. **返回结果**：格式化版本号字符串

### 数据流程
```
MCP工具调用 → 发送版本查询命令 → 电机响应 → 解析版本号 → 存储到motor_status_t → 读取版本号 → 返回结果
```

## 预期效果

### 修复1效果
- ✅ 消除活跃电机ID警告
- ✅ 正确处理无活跃电机的情况
- ✅ LED显示逻辑正常工作

### 修复2效果
- ✅ 能够主动查询电机软件版本
- ✅ 版本号正确解析和显示
- ✅ 支持语音查询版本号功能

## 测试验证

修复后应该看到：
1. **无警告信息**：不再出现"活跃电机ID 255 超出范围"警告
2. **版本号显示**：能够正确显示电机软件版本号
3. **语音查询**：通过语音可以查询到版本号

## 技术要点

1. **类型安全**：注意有符号和无符号整数的转换
2. **异步通信**：CAN通信是异步的，需要等待响应
3. **命令类型**：不同命令类型对应不同的数据解析逻辑
4. **错误处理**：完整的错误检查和日志记录

## 相关文件

- `protocol_motor.h` - 添加版本查询方法声明
- `protocol_motor.cpp` - 实现版本查询方法
- `deep_motor_led_state.cc` - 修复活跃电机ID类型问题
- `deep_motor_control.cc` - 修改MCP工具逻辑
