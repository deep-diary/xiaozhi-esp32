#include "frame_queue.hpp"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstring>

#define TAG "FaceFramePool"

bool FaceFramePool::Init(size_t buffer_count, size_t buffer_bytes) {
    if (buffer_count == 0 || buffer_bytes == 0) {
        return false;
    }
    buffers_ = static_cast<uint8_t**>(heap_caps_calloc(buffer_count, sizeof(uint8_t*), MALLOC_CAP_INTERNAL));
    if (!buffers_) {
        ESP_LOGE(TAG, "alloc buffers array failed");
        return false;
    }
    for (size_t i = 0; i < buffer_count; i++) {
        buffers_[i] = static_cast<uint8_t*>(heap_caps_malloc(buffer_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!buffers_[i]) {
            ESP_LOGE(TAG, "alloc pool buffer %zu failed, need %zu bytes", i, buffer_bytes);
            for (size_t j = 0; j < i; j++) {
                heap_caps_free(buffers_[j]);
            }
            heap_caps_free(buffers_);
            buffers_ = nullptr;
            return false;
        }
    }
    free_list_ = static_cast<void**>(heap_caps_calloc(buffer_count, sizeof(void*), MALLOC_CAP_INTERNAL));
    if (!free_list_) {
        for (size_t i = 0; i < buffer_count; i++) {
            heap_caps_free(buffers_[i]);
        }
        heap_caps_free(buffers_);
        buffers_ = nullptr;
        return false;
    }
    pool_size_ = buffer_count;
    buffer_bytes_ = buffer_bytes;
    free_count_ = buffer_count;
    for (size_t i = 0; i < buffer_count; i++) {
        free_list_[i] = buffers_[i];
    }
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) {
        heap_caps_free(free_list_);
        for (size_t i = 0; i < buffer_count; i++) {
            heap_caps_free(buffers_[i]);
        }
        heap_caps_free(buffers_);
        buffers_ = nullptr;
        free_list_ = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "pool init: %zu buffers, %zu bytes each", pool_size_, buffer_bytes_);
    return true;
}

void FaceFramePool::Deinit() {
    if (mutex_) {
        vSemaphoreDelete(static_cast<SemaphoreHandle_t>(mutex_));
        mutex_ = nullptr;
    }
    if (buffers_) {
        for (size_t i = 0; i < pool_size_; i++) {
            if (buffers_[i]) {
                heap_caps_free(buffers_[i]);
                buffers_[i] = nullptr;
            }
        }
        heap_caps_free(buffers_);
        buffers_ = nullptr;
    }
    if (free_list_) {
        heap_caps_free(free_list_);
        free_list_ = nullptr;
    }
    pool_size_ = 0;
    buffer_bytes_ = 0;
    free_count_ = 0;
}

uint8_t* FaceFramePool::TakeBuffer() {
    if (!mutex_ || !free_list_) {
        return nullptr;
    }
    if (xSemaphoreTake(static_cast<SemaphoreHandle_t>(mutex_), portMAX_DELAY) != pdTRUE) {
        return nullptr;
    }
    uint8_t* ptr = nullptr;
    if (free_count_ > 0) {
        free_count_--;
        ptr = static_cast<uint8_t*>(free_list_[free_count_]);
        free_list_[free_count_] = nullptr;
    }
    xSemaphoreGive(static_cast<SemaphoreHandle_t>(mutex_));
    return ptr;
}

void FaceFramePool::ReturnBuffer(uint8_t* ptr) {
    if (!ptr || !mutex_ || !free_list_) {
        return;
    }
    bool is_ours = false;
    for (size_t i = 0; i < pool_size_; i++) {
        if (buffers_[i] == ptr) {
            is_ours = true;
            break;
        }
    }
    if (!is_ours) {
        ESP_LOGW(TAG, "ReturnBuffer: ptr not from this pool, ignore");
        return;
    }
    if (xSemaphoreTake(static_cast<SemaphoreHandle_t>(mutex_), portMAX_DELAY) != pdTRUE) {
        return;
    }
    if (free_count_ < pool_size_) {
        free_list_[free_count_] = ptr;
        free_count_++;
    }
    xSemaphoreGive(static_cast<SemaphoreHandle_t>(mutex_));
}
