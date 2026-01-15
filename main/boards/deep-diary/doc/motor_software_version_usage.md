# 电机软件版本查询功能使用说明

## 功能概述

新增了电机软件版本查询功能，可以通过语音或MCP工具查询电机的软件版本号。软件版本号是从CAN帧数据中解析得到的，存储在`motor_status_t`结构体的`version`字段中。

## 实现细节

### 1. 数据来源
软件版本号通过以下方式获取：
```cpp
RX_DATA_DISASSEMBLE_VERSION_STR(can_frame.data, status->version, sizeof(status->version));
```

### 2. 数据结构
版本号存储在`motor_status_t`结构体中：
```cpp
typedef struct {
    // ... 其他字段 ...
    char version[9];          // 软件版本号
} motor_status_t;
```

### 3. 接口设计

#### DeepMotor类接口
```cpp
/**
 * @brief 获取电机软件版本号
 * @param motor_id 电机ID
 * @param version 版本号字符串缓冲区
 * @param buffer_size 缓冲区大小
 * @return true 成功, false 失败（电机未注册）
 */
bool getMotorSoftwareVersion(uint8_t motor_id, char* version, size_t buffer_size) const;
```

#### MCP工具接口
```cpp
"self.motor.get_software_version"  // 获取电机软件版本号
```

## 使用方法

### 1. 语音查询
用户可以通过语音说：
- "查询一号电机的软件版本"
- "电机1的软件版本是什么"
- "获取电机软件版本信息"

### 2. MCP工具调用
```cpp
// 调用示例
self.motor.get_software_version(motor_id=1)
```

### 3. 返回格式
成功时返回：
```
电机ID 1 软件版本: v1.2.3
```

失败时返回：
```
电机ID 1 未注册
```
或
```
获取电机ID 1 软件版本失败
```

## 技术实现

### 1. 版本号获取流程
1. **CAN帧接收**：接收电机反馈帧
2. **数据解析**：使用`RX_DATA_DISASSEMBLE_VERSION_STR`解析版本号
3. **状态存储**：将版本号存储在`motor_statuses_[index].version`中
4. **查询接口**：通过`getMotorSoftwareVersion`方法查询

### 2. 安全检查
- **电机注册检查**：确保电机已注册
- **缓冲区检查**：确保版本号缓冲区有效
- **字符串安全**：使用`strncpy`和null终止符确保字符串安全

### 3. 错误处理
- 电机未注册：返回失败并记录警告日志
- 缓冲区无效：返回失败并记录警告日志
- 成功查询：记录信息日志并返回版本号

## 日志输出

### 成功查询
```
I (xxx) DeepMotor: 电机ID 1 软件版本: v1.2.3
I (xxx) DeepMotorControl: 获取电机ID 1 软件版本成功: v1.2.3
```

### 失败查询
```
W (xxx) DeepMotor: 查询未注册电机ID 2 的软件版本
W (xxx) DeepMotorControl: 电机ID 2 未注册
```

## 使用场景

### 1. 系统诊断
- 检查电机固件版本
- 确认电机软件更新状态
- 故障排查时获取版本信息

### 2. 维护管理
- 批量检查电机版本
- 版本兼容性验证
- 升级计划制定

### 3. 开发调试
- 开发过程中版本确认
- 测试环境版本验证
- 问题复现时的版本信息

## 注意事项

1. **电机注册**：只有已注册的电机才能查询版本号
2. **数据更新**：版本号需要电机发送反馈帧才能更新
3. **缓冲区大小**：版本号缓冲区建议至少16字节
4. **字符串格式**：版本号格式取决于电机固件的实现

## 扩展功能

未来可以考虑添加：
- 批量查询所有电机版本号
- 版本号比较功能
- 版本号历史记录
- 自动版本检查功能
