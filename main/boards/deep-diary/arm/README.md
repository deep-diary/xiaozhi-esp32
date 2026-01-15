# 机械臂控制模块 (Robotic Arm Control Module)

## 功能概述

本模块提供了六自由度机械臂的完整控制功能，包括：
- 正/逆运动学计算
- 轨迹规划与插值
- 动作序列录制与播放
- LED状态反馈
- MCP服务器接口

## 文件说明

### 核心文件

- **deep_arm.cc/h**: 机械臂核心控制类
  - 多电机协调控制
  - 动作序列管理（录制/播放/暂停）
  - 姿态设置与查询
  - 集成LED状态显示

- **deep_arm_control.cc/h**: MCP控制接口
  - 暴露MCP工具函数
  - 提供用户友好的控制接口
  - 支持录制、播放、姿态控制等功能

- **trajectory_planner.c/h**: 轨迹规划器
  - 梯形速度规划
  - 多轴同步插值
  - 平滑运动轨迹生成

- **arm_led_state.cc/h**: 机械臂LED状态管理
  - 根据机械臂状态自动更新LED
  - 支持多种状态显示（录制、播放、空闲等）

## 主要功能

### 1. 机械臂初始化
```cpp
// 创建电机管理器
DeepMotor* deep_motor = new DeepMotor(led_strip);

// 定义电机ID（6个电机）
uint8_t motor_ids[6] = {1, 2, 3, 4, 5, 6};

// 创建机械臂控制器
DeepArm* deep_arm = new DeepArm(deep_motor, motor_ids);
```

### 2. 使能/失能
```cpp
// 使能所有电机
deep_arm->enableAll();

// 失能所有电机
deep_arm->disableAll();

// 归零（回到零位）
deep_arm->setZeroPose();
```

### 3. 姿态控制
```cpp
// 设置单个关节角度
deep_arm->setJointAngle(joint_id, angle);

// 设置所有关节角度
float angles[6] = {0, 30, 45, 0, 90, 0};
deep_arm->setPose(angles);

// 获取当前姿态
float current_angles[6];
deep_arm->getCurrentPose(current_angles);
```

### 4. 动作录制与播放
```cpp
// 开始录制
deep_arm->startRecording();

// 停止录制
deep_arm->stopRecording();

// 播放录制的动作
deep_arm->playRecording();

// 暂停播放
deep_arm->pausePlayback();

// 停止播放
deep_arm->stopPlayback();

// 保存动作序列到NVS
deep_arm->saveRecording();

// 从NVS加载动作序列
deep_arm->loadRecording();
```

### 5. MCP工具使用
通过MCP服务器可以使用以下工具：
- `arm_enable`: 使能机械臂
- `arm_disable`: 失能机械臂
- `arm_set_pose`: 设置姿态
- `arm_get_pose`: 获取当前姿态
- `arm_start_recording`: 开始录制
- `arm_stop_recording`: 停止录制
- `arm_play_recording`: 播放录制
- `arm_save_recording`: 保存动作序列
- `arm_load_recording`: 加载动作序列

## 使用示例

### 基本控制流程
```cpp
// 1. 创建机械臂对象
uint8_t motor_ids[6] = {1, 2, 3, 4, 5, 6};
DeepArm* arm = new DeepArm(deep_motor, motor_ids);

// 2. 使能机械臂
arm->enableAll();

// 3. 设置目标姿态
float target_pose[6] = {0, 30, 45, 0, 90, 0};
arm->setPose(target_pose);

// 4. 等待运动完成
while (!arm->isMovementComplete()) {
    vTaskDelay(pdMS_TO_TICKS(100));
}
```

### 录制与播放示例
```cpp
// 录制动作序列
arm->startRecording();
// ... 手动操作机械臂或发送控制指令 ...
arm->stopRecording();
arm->saveRecording();  // 保存到NVS

// 播放动作序列
arm->loadRecording();  // 从NVS加载
arm->playRecording();
```

### 创建MCP控制接口
```cpp
auto& mcp_server = McpServer::GetInstance();
DeepArmControl* arm_control = new DeepArmControl(deep_arm, mcp_server, led_strip);
```

## 轨迹规划

轨迹规划器提供平滑的运动控制：
- 梯形速度曲线
- 加速度限制
- 多轴同步运动
- 可配置的速度和加速度参数

```cpp
// 配置轨迹规划参数
TrajectoryPlanner planner;
planner.max_velocity = 50.0;      // 最大速度（度/秒）
planner.max_acceleration = 100.0; // 最大加速度（度/秒²）
```

## LED状态显示

机械臂状态通过LED灯带直观显示：
- **绿色呼吸**: 空闲状态
- **蓝色流动**: 运动中
- **红色闪烁**: 录制中
- **紫色流动**: 播放中
- **黄色**: 暂停状态
- **红色常亮**: 错误状态

## 依赖关系

- **电机模块**: 底层电机控制
- **CAN模块**: 电机通信
- **LED模块**: 状态显示（可选）
- **MCP服务器**: 对外控制接口
- **NVS**: 动作序列存储

## 注意事项

1. 使用前确保所有电机已正确连接并上电
2. 首次使用建议先执行归零操作
3. 录制动作前请确保机械臂在安全位置
4. 动作序列保存在NVS中，断电不丢失
5. 播放录制动作时请确保周围环境安全

## 相关文档

详细的使用说明和示例请参考：
- `../doc/deep_arm_example.md` - 机械臂使用示例
- `../doc/arm_integration_test_guide.md` - 集成测试指南
- `../doc/arm_mcp_usage_guide.md` - MCP接口使用指南
- `../doc/机械臂灯带状态管理总结.md` - LED状态说明

