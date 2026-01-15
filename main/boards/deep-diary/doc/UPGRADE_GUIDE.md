# 开源项目升级指南

## 概述

为了便于同步上游开源项目的更新，我们将自定义的新增功能代码独立到了 `board_extensions.cc/h` 文件中，使得主板级文件 `atk_dnesp32s3.cc` 能够尽可能保持与开源项目一致。

## 文件结构

### 原始方案（当前使用）
```
atk_dnesp32s3.cc         # 包含所有功能的单一文件（648行）
├── 基础硬件初始化
├── XL9555控制
├── 摄像头初始化
├── 云台控制
├── CAN和电机
├── LED灯带
├── 传感器
├── 流媒体
└── 各种任务
```

**优点**：所有代码在一个文件中，便于查看
**缺点**：文件臃肿，难以与开源项目同步更新

### 新方案（推荐）
```
atk_dnesp32s3_minimal.cc      # 简化的主板文件（~150行）
├── 基础硬件初始化（与开源项目保持一致）
├── 创建扩展对象
└── 最小必要的修改

board_extensions.cc/h         # 所有自定义功能（~500行）
├── XL9555控制
├── 摄像头初始化
├── 云台控制
├── CAN和电机
├── LED灯带
├── 传感器
├── 流媒体
└── 各种任务
```

**优点**：
- 主文件简洁，易于与开源项目合并
- 自定义功能集中管理
- 模块化，易于维护

**缺点**：需要管理两个文件（但这正是优点）

## 切换到新方案

### 步骤1：备份当前文件
```bash
cd main/boards/atk-dnesp32s3/
cp atk_dnesp32s3.cc atk_dnesp32s3_backup.cc
```

### 步骤2：替换主文件
```bash
mv atk_dnesp32s3.cc atk_dnesp32s3_old.cc
mv atk_dnesp32s3_minimal.cc atk_dnesp32s3.cc
```

### 步骤3：编译测试
```bash
cd /path/to/project
idf.py build
```

### 步骤4：验证功能
确保所有功能正常：
- [ ] 基础启动
- [ ] WiFi连接
- [ ] 摄像头
- [ ] 云台
- [ ] 电机控制
- [ ] LED灯带
- [ ] 传感器读取
- [ ] MJPEG视频流

## 与开源项目同步更新

### 方法1：直接替换（推荐）

当开源项目更新时：

1. **下载开源项目最新的板级文件**
```bash
# 假设开源仓库为 upstream
wget https://raw.githubusercontent.com/78/xiaozhi-esp32/main/main/boards/atk-dnesp32s3/atk_dnesp32s3.cc \
     -O atk_dnesp32s3_upstream.cc
```

2. **对比差异**
```bash
# 查看上游版本与我们的简化版本的差异
diff atk_dnesp32s3_upstream.cc atk_dnesp32s3_minimal.cc
```

3. **应用必要的修改**

通常只需要在简化版中添加少量修改：
- 添加 `#include "board_extensions.h"`
- 在构造函数中创建 `extensions_`
- 在 `StartNetwork()` 中启动MJPEG服务器
- 在 `GetCamera()` 中返回扩展对象的摄像头
- 在 `InitializeSt7789Display()` 中使用XL9555

### 方法2：使用补丁文件

1. **生成补丁文件**（首次）
```bash
# 比较开源原始文件和我们的简化版
diff -u atk_dnesp32s3_upstream_original.cc atk_dnesp32s3_minimal.cc > board_modifications.patch
```

2. **应用补丁**（升级时）
```bash
# 下载新版本
wget https://raw.githubusercontent.com/78/xiaozhi-esp32/main/main/boards/atk-dnesp32s3/atk_dnesp32s3.cc \
     -O atk_dnesp32s3_new.cc

# 应用补丁
patch atk_dnesp32s3_new.cc < board_modifications.patch

# 检查结果
mv atk_dnesp32s3_new.cc atk_dnesp32s3.cc
```

## 主文件修改说明

### 必须的修改点

#### 1. 头文件引入（文件开头）
```cpp
// ========== 新增 ==========
#include "board_extensions.h"
```

#### 2. 成员变量（private部分）
```cpp
// ========== 新增 ==========
BoardExtensions* extensions_;
```

#### 3. 构造函数（public部分）
```cpp
atk_dnesp32s3() 
    : boot_button_(BOOT_BUTTON_GPIO, false)
    , extensions_(nullptr) {  // 新增初始化
    
    InitializeI2c();
    InitializeSpi();
    
    // ========== 新增：创建扩展对象 ==========
    extensions_ = new BoardExtensions(i2c_bus_, nullptr);
    
    InitializeSt7789Display();
    InitializeButtons();
}
```

#### 4. 析构函数
```cpp
~atk_dnesp32s3() {
    // ========== 新增 ==========
    delete extensions_;
}
```

#### 5. 显示屏初始化（使用XL9555）
```cpp
void InitializeSt7789Display() {
    // ... 其他代码 ...
    
    esp_lcd_panel_reset(panel);
    
    // ========== 修改：通过扩展对象控制XL9555 ==========
    if (extensions_ && extensions_->GetXL9555()) {
        extensions_->GetXL9555()->SetOutputState(8, 1);
        extensions_->GetXL9555()->SetOutputState(2, 0);
    }
    
    // ... 其他代码 ...
}
```

#### 6. 网络启动（启动MJPEG服务器）
```cpp
virtual void StartNetwork() override {
    // ========== 新增：注册WiFi事件 ==========
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED,
        [](void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
            auto* board = static_cast<atk_dnesp32s3*>(arg);
            if (board->extensions_) {
                xTaskCreate(
                    [](void* pvParameters) {
                        auto* ext = static_cast<BoardExtensions*>(pvParameters);
                        ext->StartMjpegServerWhenReady();
                        vTaskDelete(NULL);
                    },
                    "mjpeg_starter", 8192, board->extensions_, 5, nullptr
                );
            }
        },
        this
    );
    
    WifiBoard::StartNetwork();
}
```

#### 7. 获取摄像头
```cpp
virtual Camera* GetCamera() override {
    // ========== 修改：返回扩展对象的摄像头 ==========
    return extensions_ ? extensions_->GetCamera() : nullptr;
}
```

## 代码对比

### 主文件大小对比
| 版本 | 代码行数 | 说明 |
|------|----------|------|
| 原始版本 | ~150行 | 开源项目原始代码 |
| 当前全功能版 | 648行 | 包含所有自定义功能 |
| **新简化版** | **~150行** | **基础代码 + 少量修改** |

### 扩展文件
| 文件 | 代码行数 | 说明 |
|------|----------|------|
| board_extensions.h | ~150行 | 扩展功能接口定义 |
| board_extensions.cc | ~350行 | 扩展功能实现 |

## 新增功能扩展

### 在扩展类中添加新功能

**步骤1**：在 `board_extensions.h` 中声明

```cpp
class BoardExtensions {
public:
    // 新增功能初始化
    void InitializeNewFeature();
    
    // 新增功能访问器
    NewFeatureClass* GetNewFeature() { return new_feature_; }

private:
    NewFeatureClass* new_feature_;
    
    // 如果需要任务
    TaskHandle_t new_feature_task_handle_;
    static void new_feature_task(void* pvParameters);
};
```

**步骤2**：在 `board_extensions.cc` 中实现

```cpp
void BoardExtensions::InitializeNewFeature() {
    ESP_LOGI(TAG, "初始化新功能...");
    new_feature_ = new NewFeatureClass();
    
    // 如果需要任务
    xTaskCreate(new_feature_task, "new_feature", 
                4096, this, 5, &new_feature_task_handle_);
}

void BoardExtensions::new_feature_task(void* pvParameters) {
    BoardExtensions* ext = static_cast<BoardExtensions*>(pvParameters);
    while (1) {
        // 任务逻辑
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

**步骤3**：在构造函数中调用

```cpp
BoardExtensions::BoardExtensions(...) {
    // ... 其他初始化 ...
    
    InitializeNewFeature();  // 添加这一行
}
```

**关键点**：主板级文件 `atk_dnesp32s3.cc` **完全不需要修改**！

## 回退方案

如果新方案出现问题，可以快速回退：

```bash
# 恢复原始文件
mv atk_dnesp32s3.cc atk_dnesp32s3_minimal_temp.cc
mv atk_dnesp32s3_old.cc atk_dnesp32s3.cc

# 或使用备份
cp atk_dnesp32s3_backup.cc atk_dnesp32s3.cc

# 重新编译
idf.py build
```

## 版本管理建议

### Git分支策略

```bash
main
├── boards/atk-dnesp32s3/
│   ├── atk_dnesp32s3.cc          # 当前使用的版本
│   ├── board_extensions.cc       # 扩展功能
│   ├── board_extensions.h
│   └── atk_dnesp32s3_old.cc      # 旧版备份（不提交）
```

### 提交信息建议

```
git commit -m "refactor: 分离板级扩展功能到独立文件

- 创建 board_extensions.cc/h 管理所有自定义功能
- 简化 atk_dnesp32s3.cc 便于同步上游更新
- 功能无变化，仅重构代码结构
"
```

## 常见问题

### Q: 新方案会影响性能吗？
A: 不会。只是代码组织方式的改变，编译后的代码是一样的。

### Q: 如果开源项目大幅度改动怎么办？
A: 这正是新方案的优势。你只需要关注主文件的改动，扩展文件保持不变。

### Q: 可以同时保留两个版本吗？
A: 可以，但不推荐。建议使用Git来管理版本历史。

### Q: 扩展文件可以进一步拆分吗？
A: 可以！扩展文件已经按模块组织，如果太大可以进一步拆分为多个扩展类。

## 总结

✅ **推荐使用新方案**，因为：

1. **主文件简洁**：从648行减少到~150行
2. **易于升级**：修改点明确，升级开源项目代码更容易
3. **便于维护**：自定义功能集中管理
4. **模块化好**：功能边界清晰
5. **可扩展性强**：新增功能不影响主文件

---

**最后更新**：2025年10月21日  
**文档版本**：v1.0

