# NVS键名长度问题修复

## 问题描述

在结束边界标定时，设备重启，错误信息显示：
```
ESP_ERROR_CHECK failed: esp_err_t 0x1109 (ESP_ERR_NVS_KEY_TOO_LONG)
```

## 问题原因

ESP32的NVS存储对键名长度有限制，最大15个字符。原代码中使用的键名过长：

- `"boundary_calibrated"` (18个字符) ❌
- `"boundary_min_0"` (13个字符) ✅
- `"boundary_max_0"` (13个字符) ✅

## 修复方案

将所有NVS键名缩短到15个字符以内：

### 修复前
```cpp
settings_->SetBool("boundary_calibrated", true);
std::string min_key = "boundary_min_" + std::to_string(i);
std::string max_key = "boundary_max_" + std::to_string(i);
```

### 修复后
```cpp
settings_->SetBool("bnd_cal", true);
std::string min_key = "bnd_min_" + std::to_string(i);
std::string max_key = "bnd_max_" + std::to_string(i);
```

## 键名对照表

| 功能 | 原键名 | 新键名 | 字符数 |
|------|--------|--------|--------|
| 边界标定状态 | `boundary_calibrated` | `bnd_cal` | 7 |
| 最小位置 | `boundary_min_0` | `bnd_min_0` | 9 |
| 最大位置 | `boundary_max_0` | `bnd_max_0` | 9 |

## 修复的文件

- `deep_arm.cc` - `saveBoundaryData()` 函数
- `deep_arm.cc` - `loadBoundaryData()` 函数

## 测试建议

1. **重新编译和烧录**
```bash
idf.py build
idf.py flash monitor
```

2. **测试边界标定**
```python
# 开始边界标定
self.arm.start_boundary_calibration()

# 手动转动各关节到极限位置

# 停止边界标定（之前会重启的地方）
self.arm.stop_boundary_calibration()
```

3. **验证数据保存**
```python
# 检查边界数据
self.arm.get_boundary()

# 重启设备后再次检查
self.arm.get_boundary()
```

## 注意事项

1. **数据兼容性**: 修复后，之前的边界标定数据将无法读取（因为键名改变了）
2. **重新标定**: 需要重新进行边界标定
3. **键名规范**: 今后所有NVS键名都应控制在15个字符以内

## ESP32 NVS限制

- **键名长度**: 最大15个字符
- **命名空间长度**: 最大15个字符
- **值长度**: 最大4000字节
- **键数量**: 每个命名空间最多255个键

## 预防措施

为避免类似问题，建议：
1. 使用简短的键名
2. 建立键名命名规范
3. 在开发时测试NVS操作
4. 添加键名长度检查
