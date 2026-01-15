# 功能开关宏定义说明

## 概述
本板级目录使用宏定义来控制各种功能的启用/禁用，所有宏定义集中在 `config.h` 文件中。

## 功能开关列表

### 1. 相机功能
```cpp
#define ENABLE_CAMERA_FEATURE 1  // 1:启用, 0:禁用
```
- **功能**：OV2640 摄像头采集
- **依赖**：需要 XL9555 I/O 扩展控制摄像头电源和复位
- **内存占用**：PSRAM 约 153KB（framebuffer）
- **GPIO 冲突**：与 CAN 和 LED 灯带共用部分引脚
- **自动禁用**：启用相机后自动禁用 CAN 和 LED 功能

### 2. CAN 总线功能
```cpp
#define ENABLE_CAN_FEATURE 0  // 1:启用, 0:禁用
```
- **功能**：CAN 总线通信，用于电机和机械臂控制
- **引脚**：TX=GPIO5, RX=GPIO4
- **冲突**：与相机功能冲突
- **注意**：启用相机时会自动禁用此功能

### 3. LED 灯带功能
```cpp
#define ENABLE_LED_STRIP_FEATURE 0  // 1:启用, 0:禁用
```
- **功能**：WS2812 RGB LED 灯带控制
- **引脚**：GPIO7
- **LED 数量**：24 个（可在 `WS2812_LED_COUNT` 中配置）
- **冲突**：与相机功能冲突
- **注意**：启用相机时会自动禁用此功能

### 4. QMA6100P 加速度计
```cpp
#define ENABLE_QMA6100P_FEATURE 1  // 1:启用, 0:禁用
```
- **功能**：三轴加速度计，提供加速度和姿态角数据
- **接口**：I2C
- **更新频率**：500ms（在 `user_main_loop_task` 中配置）
- **显示位置**：状态栏（使用 `SetStatus`）
- **无冲突**：可与其他功能同时使用

### 5. MJPEG 流媒体服务器
```cpp
#define ENABLE_MJPEG_FEATURE 0  // 1:启用, 0:禁用(默认)
```
- **功能**：HTTP MJPEG 视频流服务器
- **端口**：8080
- **帧率**：10fps（可配置）
- **质量**：80（可配置）
- **依赖**：需要 `ENABLE_CAMERA_FEATURE 1`
- **内存占用**：较高（HTTP 服务器 + JPEG 编码）
- **计划**：将来迁移到 RTSP 推流到 MediaMTX 服务器
- **注意**：默认禁用以节省内存

## GPIO 冲突矩阵

| 引脚 | 相机 | CAN | LED | 备注 |
|------|------|-----|-----|------|
| GPIO4 | D0 | RX | - | 冲突 |
| GPIO5 | D1 | TX | - | 冲突 |
| GPIO7 | D3 | - | WS2812 | 冲突 |

**结论**：相机、CAN、LED 三者不能同时启用。

## 自动冲突处理

在 `config.h` 中有自动冲突处理逻辑：

```cpp
// 当启用相机功能时，自动禁用冲突的功能
#if ENABLE_CAMERA_FEATURE
    #undef ENABLE_CAN_FEATURE
    #undef ENABLE_LED_STRIP_FEATURE
    #define ENABLE_CAN_FEATURE 0
    #define ENABLE_LED_STRIP_FEATURE 0
#endif
```

## 推荐配置

### 配置 1：智能语音助手（默认）
```cpp
#define ENABLE_CAMERA_FEATURE 1       // 启用相机
#define ENABLE_CAN_FEATURE 0          // 禁用CAN
#define ENABLE_LED_STRIP_FEATURE 0    // 禁用LED
#define ENABLE_QMA6100P_FEATURE 1     // 启用加速度计
#define ENABLE_MJPEG_FEATURE 0        // 禁用MJPEG（节省内存）
```
- **用途**：语音助手 + 拍照 + 姿态检测
- **内存占用**：中等

### 配置 2：机器人控制
```cpp
#define ENABLE_CAMERA_FEATURE 0       // 禁用相机
#define ENABLE_CAN_FEATURE 1          // 启用CAN
#define ENABLE_LED_STRIP_FEATURE 1    // 启用LED
#define ENABLE_QMA6100P_FEATURE 1     // 启用加速度计
#define ENABLE_MJPEG_FEATURE 0        // N/A（相机已禁用）
```
- **用途**：电机控制 + 机械臂 + 灯效
- **内存占用**：低

### 配置 3：视频流演示（测试用）
```cpp
#define ENABLE_CAMERA_FEATURE 1       // 启用相机
#define ENABLE_CAN_FEATURE 0          // 禁用CAN
#define ENABLE_LED_STRIP_FEATURE 0    // 禁用LED
#define ENABLE_QMA6100P_FEATURE 0     // 禁用加速度计（可选）
#define ENABLE_MJPEG_FEATURE 1        // 启用MJPEG
```
- **用途**：视频流测试
- **内存占用**：高
- **访问**：`http://<IP>:8080/stream`

## 修改宏定义后的操作

1. 修改 `main/boards/atk-dnesp32s3/config.h`
2. 重新编译：`idf.py build`
3. 烧录固件：`idf.py flash`
4. 查看日志：`idf.py monitor`

## 代码规范

### ✅ 推荐做法
```cpp
// 在头文件中使用宏控制声明
#if ENABLE_MJPEG_FEATURE
class MjpegServer;
void InitializeMjpegServer();
#endif

// 在实现文件中用宏包裹整块代码
#if ENABLE_MJPEG_FEATURE
void BoardExtensions::InitializeMjpegServer() {
    // 实现代码
}
#endif
```

### ❌ 避免的做法
```cpp
// 避免在函数内部到处使用宏
void SomeFunction() {
    DoSomething();
    #if ENABLE_FEATURE_A
    DoA();
    #endif
    DoSomethingElse();
    #if ENABLE_FEATURE_B
    DoB();
    #endif
    // 代码变得难以阅读
}
```

## 调试技巧

### 查看当前启用的功能
在编译输出中搜索：
```
I (xxx) board_ext: 初始化相机功能...
I (xxx) board_ext: 初始化QMA6100P加速度计...
I (xxx) board_ext: 初始化CAN总线...
```

### 验证宏定义
在代码中添加编译时检查：
```cpp
#if ENABLE_CAMERA_FEATURE && ENABLE_CAN_FEATURE
#error "Camera and CAN cannot be enabled at the same time!"
#endif
```

## 相关文档
- [STREAMING_MIGRATION_STATUS.md](STREAMING_MIGRATION_STATUS.md) - 流媒体迁移状态
- [streaming/RTSP_MIGRATION_PLAN.md](streaming/RTSP_MIGRATION_PLAN.md) - RTSP 迁移计划
- [sensor/README_QMA6100P.md](sensor/README_QMA6100P.md) - 加速度计使用说明

