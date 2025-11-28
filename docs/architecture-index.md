# 架构文档索引

本文档是小智 ESP32 项目架构文档的总索引，帮助您快速找到所需的信息。

## 📚 文档列表

### 1. [系统架构总览](./architecture-overview.md)

**内容概览：**
- 项目简介
- 系统架构分层（应用层、服务层、协议层、硬件抽象层）
- 核心模块说明
- 数据流向概述
- 关键设计模式
- 线程模型
- 存储管理
- 网络架构
- 扩展性设计
- 配置系统
- 错误处理
- 性能优化
- 安全考虑

**适合阅读人群：**
- 项目新成员
- 需要快速了解整体架构的开发者
- 系统设计评审人员

**阅读时间：** 约 15-20 分钟

---

### 2. [软件架构 UML 类图](./architecture-uml.md)

**内容概览：**
- 核心类图总览
- 音频处理类图
- 硬件抽象层类图
- MCP 协议类图
- 协议层类图
- 设备状态管理类图
- 网络接口类图
- 工具和资源管理类图
- 类关系说明
- 设计模式应用

**适合阅读人群：**
- 需要理解代码结构的开发者
- 需要扩展功能的开发者
- 代码审查人员

**阅读时间：** 约 20-25 分钟

---

### 3. [数据流图](./architecture-dataflow.md)

**内容概览：**
- 音频数据流（输入/输出）
- 音频处理详细流程
- 控制消息流（设备→服务器，服务器→设备）
- MCP 工具调用流程
- 设备状态流转数据流
- 网络连接数据流（WebSocket、MQTT+UDP）
- 唤醒词检测数据流
- OTA 升级数据流
- 资源文件加载数据流
- 数据流总结

**适合阅读人群：**
- 需要理解数据处理的开发者
- 调试音频问题的开发者
- 优化性能的开发者

**阅读时间：** 约 25-30 分钟

---

### 4. [状态机流程图](./architecture-statemachine.md)

**内容概览：**
- 设备主状态机（完整状态转换图）
- 状态详细说明
- 协议连接状态机（WebSocket、MQTT+UDP）
- 音频服务状态机
- 网络连接状态机（WiFi、ML307）
- MCP 会话状态机
- 唤醒词检测状态机
- OTA 升级状态机
- 电源管理状态机
- 状态转换表
- 状态同步机制
- 状态机设计原则

**适合阅读人群：**
- 需要理解系统行为的开发者
- 调试状态相关问题的开发者
- 添加新状态的开发者

**阅读时间：** 约 20-25 分钟

---

### 5. [模块交互序列图](./architecture-sequence.md)

**内容概览：**
- 系统启动序列
- 唤醒词触发交互序列
- 语音交互完整序列
- MCP 工具调用序列
- WebSocket 连接建立序列
- MQTT + UDP 连接建立序列
- 音频数据传输序列（WebSocket、MQTT+UDP）
- OTA 升级序列
- 资源文件下载序列
- 设备状态变化通知序列
- 错误处理序列
- 电源管理序列
- 双网络切换序列
- 序列图总结

**适合阅读人群：**
- 需要理解模块交互的开发者
- 调试交互问题的开发者
- 添加新功能的开发者

**阅读时间：** 约 30-35 分钟

---

## 🗺️ 快速导航

### 按主题查找

#### 想了解整体架构？
→ [系统架构总览](./architecture-overview.md)

#### 想理解代码结构？
→ [软件架构 UML 类图](./architecture-uml.md)

#### 想了解数据处理？
→ [数据流图](./architecture-dataflow.md)

#### 想理解系统行为？
→ [状态机流程图](./architecture-statemachine.md)

#### 想了解模块交互？
→ [模块交互序列图](./architecture-sequence.md)

### 按角色查找

#### 我是新成员
1. 先读：[系统架构总览](./architecture-overview.md)
2. 再读：[软件架构 UML 类图](./architecture-uml.md)
3. 深入：[数据流图](./architecture-dataflow.md)

#### 我要添加新功能
1. 理解架构：[系统架构总览](./architecture-overview.md)
2. 查看类结构：[软件架构 UML 类图](./architecture-uml.md)
3. 理解交互：[模块交互序列图](./architecture-sequence.md)

#### 我要调试问题
1. 理解数据流：[数据流图](./architecture-dataflow.md)
2. 理解状态机：[状态机流程图](./architecture-statemachine.md)
3. 理解交互：[模块交互序列图](./architecture-sequence.md)

#### 我要优化性能
1. 理解数据流：[数据流图](./architecture-dataflow.md)
2. 理解架构：[系统架构总览](./architecture-overview.md)

### 按功能模块查找

#### 音频处理
- [数据流图 - 音频数据流](./architecture-dataflow.md#1-音频数据流)
- [软件架构 UML - 音频处理类图](./architecture-uml.md#2-音频处理类图)
- [模块交互序列图 - 语音交互](./architecture-sequence.md#3-语音交互完整序列)

#### 网络通信
- [数据流图 - 网络连接数据流](./architecture-dataflow.md#5-网络连接数据流)
- [状态机流程图 - 协议连接状态机](./architecture-statemachine.md#2-协议连接状态机)
- [模块交互序列图 - 连接建立](./architecture-sequence.md#5-websocket-连接建立序列)

#### MCP 协议
- [数据流图 - MCP 工具调用流程](./architecture-dataflow.md#3-mcp-工具调用流程)
- [软件架构 UML - MCP 协议类图](./architecture-uml.md#4-mcp-协议类图)
- [模块交互序列图 - MCP 工具调用](./architecture-sequence.md#4-mcp-工具调用序列)

#### 硬件抽象
- [软件架构 UML - 硬件抽象层类图](./architecture-uml.md#3-硬件抽象层类图)
- [系统架构总览 - 硬件抽象层](./architecture-overview.md#224-硬件抽象层-hal-layer)

#### 设备状态
- [状态机流程图 - 设备主状态机](./architecture-statemachine.md#1-设备主状态机)
- [软件架构 UML - 设备状态管理类图](./architecture-uml.md#6-设备状态管理类图)
- [模块交互序列图 - 状态变化通知](./architecture-sequence.md#11-设备状态变化通知序列)

## 📖 推荐阅读顺序

### 初学者路径（约 2 小时）

1. **第一步：整体了解**（30分钟）
   - [系统架构总览](./architecture-overview.md)
   - 重点：理解分层架构和核心模块

2. **第二步：代码结构**（40分钟）
   - [软件架构 UML 类图](./architecture-uml.md)
   - 重点：理解类关系和设计模式

3. **第三步：数据流**（30分钟）
   - [数据流图](./architecture-dataflow.md)
   - 重点：理解音频数据流和控制消息流

4. **第四步：系统行为**（20分钟）
   - [状态机流程图](./architecture-statemachine.md)
   - 重点：理解设备状态转换

### 开发者路径（约 3 小时）

1. **第一步：全面了解**（45分钟）
   - [系统架构总览](./architecture-overview.md)
   - [软件架构 UML 类图](./architecture-uml.md)

2. **第二步：深入理解**（60分钟）
   - [数据流图](./architecture-dataflow.md)
   - [状态机流程图](./architecture-statemachine.md)

3. **第三步：交互细节**（75分钟）
   - [模块交互序列图](./architecture-sequence.md)
   - 重点：理解关键场景的交互流程

### 专家路径（按需查阅）

- 根据具体任务查阅相关文档
- 重点关注：[模块交互序列图](./architecture-sequence.md) 和 [数据流图](./architecture-dataflow.md)

## 🔗 相关文档

### 协议文档
- [WebSocket 协议文档](./websocket.md)
- [MQTT + UDP 协议文档](./mqtt-udp.md)
- [MCP 协议文档](./mcp-protocol.md)
- [MCP 使用指南](./mcp-usage.md)

### 技术实现文档
- [AEC (回声消除) 实现原理详解](./aec-implementation.md)
  - 设备端 AEC vs 服务端 AEC 对比
  - 实现原理和代码分析
  - 使用场景和配置指南
- [设备激活流程详解](./device-activation.md)
  - 激活码生成和来源
  - 完整激活流程和数据流
  - HMAC 验证机制
  - 安全特性和错误处理

### 硬件文档
- [自定义板卡开发指南](./custom-board.md)

### 项目文档
- [README 中文版](../README_zh.md)
- [README 英文版](../README.md)

## 📝 文档维护

### 更新记录

- **2024-12**: 创建架构文档系列
  - 系统架构总览
  - 软件架构 UML 类图
  - 数据流图
  - 状态机流程图
  - 模块交互序列图

### 反馈与贡献

如果您发现文档中的问题或有改进建议，请：
1. 提交 Issue
2. 提交 Pull Request
3. 联系项目维护者

## 🎯 使用建议

1. **首次阅读**：按照推荐阅读顺序，逐步理解系统架构
2. **日常开发**：根据具体任务查阅相关章节
3. **问题调试**：结合多个文档，全面理解问题场景
4. **功能扩展**：参考相关模块的设计模式，保持架构一致性

## 📌 重要提示

- 所有图表使用 Mermaid 语法，可在支持 Mermaid 的 Markdown 查看器中正常显示
- 建议使用支持 Mermaid 的编辑器（如 VS Code + Markdown Preview Mermaid Support）
- 在线查看可以使用 [Mermaid Live Editor](https://mermaid.live/)

---

**祝您阅读愉快！如有问题，欢迎反馈。**

