# 流媒体功能迁移状态

## ✅ 已完成的修改

### 1. 使用宏定义控制 MJPEG 功能
通过 `ENABLE_MJPEG_FEATURE` 宏定义优雅地控制 MJPEG 服务器功能，以下文件已更新：

#### config.h
- ✅ 添加 `ENABLE_MJPEG_FEATURE` 宏定义（默认为 0 禁用）
- ✅ 添加说明注释：计划迁移到 RTSP

#### board_extensions.h
- ✅ 使用 `#if ENABLE_MJPEG_FEATURE` 控制 `MjpegServer` 前向声明
- ✅ 使用宏控制 `InitializeMjpegServer()` 方法声明
- ✅ 使用宏控制 `StartMjpegServerWhenReady()` 方法声明  
- ✅ 使用宏控制 `GetMjpegServer()` 访问器
- ✅ 使用宏控制 `mjpeg_server_` 成员变量

#### board_extensions.cc
- ✅ 使用 `#if ENABLE_MJPEG_FEATURE` 控制头文件包含
- ✅ 使用宏控制 `InitializeControls()` 中的初始化调用
- ✅ 使用宏包裹 `InitializeMjpegServer()` 函数实现
- ✅ 使用宏包裹 `StartMjpegServerWhenReady()` 函数实现

#### atk_dnesp32s3.cc
- ✅ 使用宏控制 WiFi 连接事件处理器中的 MJPEG 服务器启动代码

### 2. 保留的功能
- ✅ 相机初始化和采集功能（`InitializeCamera()`）
- ✅ 相机垂直翻转修正（`SetVFlip(true)`）
- ✅ 加速度计数据显示
- ✅ 其他所有板级功能

### 3. 创建的文档
- ✅ `RTSP_MIGRATION_PLAN.md` - RTSP 迁移详细计划
- ✅ `STREAMING_MIGRATION_STATUS.md` - 本文档

## ⏳ 等待实现

### RTSP 推流到 MediaMTX
详见 [`streaming/RTSP_MIGRATION_PLAN.md`](streaming/RTSP_MIGRATION_PLAN.md)

**核心目标**：
1. 实现轻量级 RTSP 客户端
2. 支持 MJPEG over RTP 或 H.264 编码
3. 推流到 MediaMTX 服务器
4. 支持多客户端观看

**预期时间**：3-4天

### 2. 修复加速度计显示问题
- ✅ 将加速度计数据从聊天消息区改为状态栏显示
- ✅ 使用 `SetStatus()` 而不是 `SetChatMessage()` 
- ✅ 简化显示格式以适应状态栏空间
- ✅ 数据更新频率保持 500ms

## 📋 验证清单

### 编译验证
- ✅ `idf.py build` 无错误
- ✅ 无 MjpegServer 相关的未定义引用
- ✅ 无头文件缺失错误

### 运行验证
- ✅ 系统正常启动
- ✅ 相机初始化成功并启用垂直翻转
- ✅ 加速度计数据显示在状态栏（每500ms更新）
- ✅ WiFi 连接正常
- ✅ 无 MJPEG 相关错误日志（因为已禁用）

## 🗑️ 可选清理（暂不执行）

以下文件暂时保留，待 RTSP 功能实现后再决定是否删除：

```
streaming/
├── mjpeg_server.h       # 保留作为参考
├── mjpeg_server.cc      # 保留作为参考
└── README.md            # 保留文档
```

**原因**：
1. 可能需要参考 MJPEG 的实现逻辑
2. RTSP 实现可能复用部分代码结构
3. 便于对比两种方案的差异

## 🎛️ 如何启用/禁用 MJPEG

在 `config.h` 中修改宏定义：

```cpp
// 禁用 MJPEG（默认，节省内存）
#define ENABLE_MJPEG_FEATURE 0

// 启用 MJPEG
#define ENABLE_MJPEG_FEATURE 1
```

**重新编译后生效**：`idf.py build flash`

## 📝 注意事项

1. **内存优化**：禁用 MJPEG 服务器后，系统内存压力显著降低，UDP 发送失败 (errno=12) 问题已改善

2. **相机功能**：虽然禁用了流媒体服务器，但相机采集功能仍然正常，可以用于：
   - 图像识别
   - 拍照保存
   - 本地显示
   - 未来的 RTSP 推流

3. **加速度计显示**：数据现在显示在状态栏，不会被聊天消息覆盖，更新频率为500ms

4. **代码清晰度**：使用宏定义而非注释，保持代码结构清晰：
   - 宏定义集中在 `config.h`
   - 避免在函数内部使用过多宏
   - 相关功能代码使用宏块包裹
   
5. **升级路径**：当前架构为 RTSP 实现预留了空间，迁移时只需：
   - 创建 `RtspPusher` 类
   - 在 `BoardExtensions` 中添加 RTSP 初始化
   - 设置 `ENABLE_MJPEG_FEATURE 0` 并启用 RTSP

## 🔗 相关资源

- [RTSP 迁移计划](streaming/RTSP_MIGRATION_PLAN.md)
- [Streaming 模块文档](streaming/README.md)
- [MediaMTX 项目](https://github.com/bluenviron/mediamtx)

---

**更新时间**：2025-10-22  
**状态**：✅ MJPEG 已禁用，等待 RTSP 实现

