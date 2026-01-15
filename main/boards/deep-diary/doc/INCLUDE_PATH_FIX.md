# Include路径修复说明

## 问题原因

在将功能模块化到子目录后，模块之间的相互引用需要更新include路径。

## Include路径规则

### 1. 从板级根目录引用（如 board_extensions.cc）

**位置**：`main/boards/atk-dnesp32s3/board_extensions.cc`

**规则**：使用相对于板级目录的路径

```cpp
// ✅ 正确
#include "motor/deep_motor.h"
#include "arm/deep_arm.h"
#include "gimbal/Gimbal.h"
#include "sensor/QMA6100P/qma6100p.h"

// ❌ 错误
#include "deep_motor.h"        // 找不到
#include "../motor/deep_motor.h" // 不需要 ../
```

### 2. 从子目录引用其他子目录（如 arm/deep_arm.h 引用 motor/）

**位置**：`main/boards/atk-dnesp32s3/arm/deep_arm.h`

**规则**：使用 `../` 返回上级目录

```cpp
// ✅ 正确
#include "../motor/deep_motor.h"
#include "../motor/protocol_motor.h"

// ❌ 错误
#include "motor/deep_motor.h"   // 会在 arm/motor/ 中找
#include "deep_motor.h"         // 找不到
```

### 3. 引用同一目录的文件

**位置**：`main/boards/atk-dnesp32s3/arm/deep_arm_control.h`

**规则**：直接使用文件名

```cpp
// ✅ 正确
#include "deep_arm.h"
#include "arm_led_state.h"

// ❌ 不推荐（虽然能工作）
#include "./deep_arm.h"
```

### 3.5. 引用板级根目录的配置文件（config.h）

**位置**：子目录中的文件（如 `led/led_control.cc`）

**规则**：使用 `../config.h`

```cpp
// ✅ 正确
#include "../config.h"

// ❌ 错误
#include "config.h"  // 会在当前目录找，找不到
```

### 4. 引用主项目的公共文件（如 led/、mcp_server.h）

**位置**：任何板级文件

**规则**：使用项目根路径（ESP-IDF会将main/添加到搜索路径）

```cpp
// ✅ 正确
#include "led/circular_strip.h"   // 引用 main/led/circular_strip.h
#include "mcp_server.h"            // 引用 main/mcp_server.h
#include "display/lcd_display.h"   // 引用 main/display/lcd_display.h

// ❌ 错误
#include "../../../led/circular_strip.h"  // 太复杂
```

## 已修复的文件

### 1. arm/deep_arm.h

**修改前**：
```cpp
#include "deep_motor.h"
#include "protocol_motor.h"
```

**修改后**：
```cpp
#include "../motor/deep_motor.h"
#include "../motor/protocol_motor.h"
```

### 2. gimbal/Gimbal.h

**修改前**：
```cpp
#include "Servo.h"
```

**修改后**：
```cpp
#include "../servo/Servo.h"
```

### 3. led/led_control.cc

**修改前**：
```cpp
#include "config.h"
```

**修改后**：
```cpp
#include "../config.h"
```

## 完整的Include路径示例

### arm/deep_arm.h
```cpp
#include <stdint.h>              // 系统头文件
#include "../motor/deep_motor.h"  // 其他子目录
#include "trajectory_planner.h"   // 同目录文件
#include "settings.h"             // 主项目文件
```

### motor/deep_motor.h
```cpp
#include "protocol_motor.h"       // 同目录文件
#include "led/circular_strip.h"   // 主项目文件
#include "deep_motor_led_state.h" // 同目录文件
```

### gimbal/Gimbal.h
```cpp
#include "../servo/Servo.h"       // 其他子目录
#include "driver/twai.h"          // ESP-IDF系统文件
```

## 检查清单

在添加新文件时，检查以下几点：

- [ ] 引用其他子目录的文件时使用 `../目录名/文件.h`
- [ ] 引用同目录的文件时直接使用 `文件.h`
- [ ] 引用主项目文件时使用 `目录名/文件.h`
- [ ] 引用系统文件时使用 `<文件.h>` 或 `<目录/文件.h>`

## 常见错误

### 错误1：找不到头文件
```
fatal error: deep_motor.h: No such file or directory
```
**原因**：没有使用正确的相对路径  
**解决**：添加 `../motor/` 前缀

### 错误2：包含了错误的文件
```
error: 'SomeClass' has not been declared
```
**原因**：可能找到了同名但不同的文件  
**解决**：检查include路径是否正确

### 错误3：循环依赖
```
error: 'SomeClass' was not declared in this scope
```
**原因**：两个头文件相互包含  
**解决**：使用前向声明（forward declaration）

## 构建系统说明

ESP-IDF的CMake构建系统会自动将以下目录添加到include搜索路径：
- `main/` - 主项目目录
- `main/boards/板名/` - 当前板级目录
- ESP-IDF组件目录

因此：
- 引用 `main/` 下的文件：直接使用相对于 `main/` 的路径
- 引用板级目录下的文件：使用相对路径
- 引用ESP-IDF组件：直接使用组件名/文件名

## 验证方法

编译时如果出现找不到头文件的错误：
```bash
idf.py build
```

检查错误信息中的文件路径，确定正确的相对路径。

## 总结

遵循以下简单规则：
1. **同目录** → 直接用文件名
2. **其他子目录** → 用 `../目录名/文件名`
3. **主项目** → 用 `目录名/文件名`（从main/算起）
4. **系统** → 用 `<头文件>`

---

**最后更新**：2025年10月22日

