# 内存优化建议

## 问题描述
系统运行时出现 `E (xxxxx) EspUdp: Send failed: ret=-1, errno=12` 错误。

**错误分析**：
- `errno=12` 是 `ENOMEM` (Out of Memory)
- 错误位置：`managed_components/78__esp-ml307/src/esp/esp_udp.cc:91`
- 触发场景：UDP 数据包发送时内存不足

## 内存压力来源
同时运行的服务：
1. **摄像头采集** - QVGA (320x240) RGB565, fb_count=1, 使用 PSRAM
2. **MJPEG 服务器** - HTTP 服务 + JPEG 编码 (质量80, 帧率10fps)
3. **加速度计** - 每200ms读取并显示数据
4. **语音识别** - 唤醒词检测 + AFE 音频处理
5. **UDP/MQTT** - 网络通信
6. **显示更新** - LVGL 图形界面

## 优化方案

### 1. 降低加速度计更新频率 ⭐ 推荐
当前：每 200ms (5Hz)
建议：每 500ms-1000ms (2-1Hz)

```cpp
// board_extensions.cc:382 行
if (update_counter >= 50 && ext->qma6100p_initialized_) {  // 改为50 = 500ms
```

**优点**：减少显示更新和内存分配压力

### 2. 降低 MJPEG 帧率和质量
当前配置：
```cpp
mjpeg_server_->SetFrameRate(10);    // 10fps
mjpeg_server_->SetJpegQuality(80);  // 质量80
```

建议配置：
```cpp
mjpeg_server_->SetFrameRate(5);     // 降低到5fps
mjpeg_server_->SetJpegQuality(60);  // 降低质量到60
```

**优点**：显著减少内存和带宽压力

### 3. 增加任务栈大小
当前用户主循环任务栈：8192 字节

```cpp
// board_extensions.cc:312
xTaskCreate(user_main_loop_task, "user_main_loop", 
            8192,  // 可尝试增加到 10240
```

**优点**：避免栈溢出导致的内存碎片

### 4. 优化显示更新 (可选)
仅在数据变化超过阈值时更新显示，减少不必要的刷新：

```cpp
static float last_acc_x = 0, last_acc_y = 0, last_acc_z = 0;
if (abs(accel_data.acc_x - last_acc_x) > 0.5 ||
    abs(accel_data.acc_y - last_acc_y) > 0.5 ||
    abs(accel_data.acc_z - last_acc_z) > 0.5) {
    // 更新显示
    last_acc_x = accel_data.acc_x;
    last_acc_y = accel_data.acc_y;
    last_acc_z = accel_data.acc_z;
}
```

### 5. 监控内存使用
添加内存监控代码：

```cpp
size_t free_heap = esp_get_free_heap_size();
size_t min_free_heap = esp_get_minimum_free_heap_size();
ESP_LOGI(TAG, "Free heap: %u, Min free: %u", free_heap, min_free_heap);
```

## 实施建议
1. **立即实施**：方案1 (降低加速度计更新频率)
2. **如果问题持续**：方案2 (降低MJPEG参数)
3. **长期优化**：方案4 + 方案5

## 备注
- ESP32-S3 有 8MB PSRAM，摄像头 framebuffer 使用 PSRAM
- 内存不足通常是内部 SRAM 问题，不是 PSRAM
- UDP 发送缓冲区在内部 SRAM 中分配

