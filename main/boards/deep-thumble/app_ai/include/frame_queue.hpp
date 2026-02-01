#pragma once

#include <cstddef>
#include <cstdint>

// 双队列架构：可入队的帧结构（1=RGB565, 2=RGB888, 3=YUYV）
struct QueuedFrame {
    uint8_t* data = nullptr;
    size_t len = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    int format = 0;
};

// 帧缓冲池：固定数量 N 个 PSRAM buffer，供相机任务取、显示端归还
class FaceFramePool {
public:
    bool Init(size_t buffer_count, size_t buffer_bytes);
    void Deinit();

    uint8_t* TakeBuffer();
    void ReturnBuffer(uint8_t* ptr);

private:
    uint8_t** buffers_ = nullptr;
    void** free_list_ = nullptr;
    size_t pool_size_ = 0;
    size_t buffer_bytes_ = 0;
    size_t free_count_ = 0;
    void* mutex_ = nullptr;
};
