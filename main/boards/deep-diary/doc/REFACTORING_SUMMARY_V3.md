# 板级代码重构总结 V3

## 📅 更新日期
2025-10-22

## ✅ 本次更新内容

### 1. 优化 MJPEG 功能控制方式
**问题**：之前使用注释方式禁用 MJPEG 功能，代码凌乱且不便于管理

**解决方案**：
- 在 `config.h` 中添加 `ENABLE_MJPEG_FEATURE` 宏定义
- 使用 `#if ENABLE_MJPEG_FEATURE` 条件编译替代注释
- 保持代码结构清晰，避免到处使用宏

**修改的文件**：
- `config.h` - 添加宏定义
- `board_extensions.h` - 使用宏控制声明
- `board_extensions.cc` - 使用宏包裹实现
- `atk_dnesp32s3.cc` - 使用宏控制启动代码

**代码规范**：
```cpp
// ✅ 推荐：用宏包裹整块代码
#if ENABLE_MJPEG_FEATURE
void InitializeMjpegServer() {
    // 完整实现
}
#endif

// ❌ 避免：函数内部到处使用宏
void SomeFunction() {
    #if ENABLE_FEATURE_A
    DoA();
    #endif
    #if ENABLE_FEATURE_B
    DoB();
    #endif
}
```

### 2. 修复加速度计数据不显示问题
**问题**：加速度计数据虽然读取正常，但没有显示在屏幕上

**根本原因**：
- 使用了 `SetChatMessage()` 显示数据
- 聊天消息会被语音对话快速覆盖
- 状态信息应该使用 `SetStatus()` 显示在状态栏

**解决方案**：
```cpp
// 之前：显示在聊天消息区（会被覆盖）
ext->display_->SetChatMessage("system", msg_buffer);

// 现在：显示在状态栏（持久显示）
ext->display_->SetStatus(msg_buffer);
```

**显示效果**：
```
状态栏: 加速度 X:-0.7 Y:0.0 Z:10.7 俯仰:4° 翻滚:0°
```

## 🎛️ 功能开关宏定义

所有功能开关集中在 `config.h`：

```cpp
#define ENABLE_CAMERA_FEATURE 1          // 相机功能
#define ENABLE_CAN_FEATURE 0             // CAN总线
#define ENABLE_LED_STRIP_FEATURE 0       // WS2812灯带
#define ENABLE_QMA6100P_FEATURE 1        // 加速度计
#define ENABLE_MJPEG_FEATURE 0           // MJPEG流媒体（默认禁用）
```

详细说明见：[FEATURE_FLAGS.md](FEATURE_FLAGS.md)

## 📊 内存优化效果

| 功能状态 | 内存占用 | UDP错误 | 说明 |
|---------|---------|---------|------|
| MJPEG 启用 | 高 | 频繁 (errno=12) | HTTP服务器 + JPEG编码 |
| MJPEG 禁用 | 中等 | 偶尔 | 仅相机采集 |
| 相机禁用 | 低 | 无 | 最低内存占用 |

**结论**：禁用 MJPEG 后，内存压力显著降低，UDP 发送失败问题已改善。

## 🏗️ 代码架构优化

### 模块化设计
```
atk-dnesp32s3/
├── config.h                 # 统一的功能开关
├── board_extensions.h/cc    # 扩展功能封装
├── atk_dnesp32s3.cc         # 主板文件（最小改动）
├── motor/                   # 电机控制模块
├── arm/                     # 机械臂控制模块
├── gimbal/                  # 云台控制模块
├── led/                     # LED控制模块
├── sensor/                  # 传感器模块
├── streaming/               # 流媒体模块
│   ├── mjpeg_server.h/cc   # MJPEG服务器（可选）
│   └── RTSP_MIGRATION_PLAN.md
└── doc/                     # 文档目录
```

### 升级友好设计
- 主板文件 `atk_dnesp32s3.cc` 保持最小修改
- 自定义功能封装在 `BoardExtensions` 类
- 便于合并开源项目更新

## 📝 新增文档

1. **FEATURE_FLAGS.md** - 功能开关详细说明
   - 所有宏定义的用途和配置
   - GPIO 冲突矩阵
   - 推荐配置方案
   - 代码规范说明

2. **STREAMING_MIGRATION_STATUS.md** - 流媒体迁移状态（已更新）
   - 宏定义控制方式
   - 加速度计显示修复
   - 如何启用/禁用 MJPEG

3. **streaming/RTSP_MIGRATION_PLAN.md** - RTSP 迁移计划
   - MediaMTX 服务器配置
   - RTSP 推流架构设计
   - 实施时间表

## 🚀 后续计划

### 短期（1周内）
- ✅ 修复加速度计显示
- ✅ 优化 MJPEG 功能控制
- ✅ 完善文档

### 中期（1-2周）
- ⏳ 测试不同功能组合
- ⏳ 优化内存使用
- ⏳ 性能调优

### 长期（1个月）
- ⏳ 实现 RTSP 推流到 MediaMTX
- ⏳ 支持 H.264 编码
- ⏳ 多客户端流媒体支持

## 🔄 版本历史

### V3 (2025-10-22)
- 使用宏定义控制 MJPEG 功能
- 修复加速度计显示问题
- 新增 FEATURE_FLAGS.md 文档
- 更新 STREAMING_MIGRATION_STATUS.md

### V2 (2025-10-22)
- 创建 BoardExtensions 类封装扩展功能
- 简化主板文件
- 模块化代码结构

### V1 (2025-10-22)
- 初始模块化重构
- 创建功能子目录
- 添加各模块 README

## 📋 验证清单

- [x] 编译通过无错误
- [x] 加速度计数据显示在状态栏
- [x] 相机正常工作（垂直翻转已启用）
- [x] MJPEG 功能可通过宏开关
- [x] 云台控制正常
- [x] 内存占用优化
- [x] 代码结构清晰
- [x] 文档完善

## 💡 最佳实践

1. **功能开关管理**
   - 所有宏定义集中在 `config.h`
   - 添加清晰的注释说明
   - 处理功能冲突

2. **代码可读性**
   - 使用宏包裹完整功能块
   - 避免函数内部频繁使用宏
   - 保持代码结构清晰

3. **模块化设计**
   - 相关功能放在同一目录
   - 每个模块有独立的 README
   - 明确的接口定义

4. **文档维护**
   - 及时更新文档
   - 记录重要决策
   - 提供使用示例

## 🔗 相关文档索引

- [FEATURE_FLAGS.md](FEATURE_FLAGS.md) - 功能开关说明
- [STREAMING_MIGRATION_STATUS.md](STREAMING_MIGRATION_STATUS.md) - 流媒体迁移
- [streaming/RTSP_MIGRATION_PLAN.md](streaming/RTSP_MIGRATION_PLAN.md) - RTSP计划
- [sensor/README_QMA6100P.md](sensor/README_QMA6100P.md) - 加速度计
- [motor/README.md](motor/README.md) - 电机控制
- [gimbal/README.md](gimbal/README.md) - 云台控制

---

**维护者**：DeepDiary 团队  
**更新日期**：2025-10-22  
**状态**：✅ 完成

