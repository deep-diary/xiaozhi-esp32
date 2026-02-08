#ifndef CAMERA_H
#define CAMERA_H

#include <cstddef>
#include <cstdint>
#include <string>

// 板级/ app_ai 取帧适配：可选填充，取最后一帧（1=RGB565, 2=RGB888, 3=YUYV）
struct CameraFrame {
    uint8_t* data = nullptr;
    size_t len = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    int format = 0;
};

class Camera {
public:
    virtual void SetExplainUrl(const std::string& url, const std::string& token) = 0;
    virtual bool Capture() = 0;
    virtual bool SetHMirror(bool enabled) = 0;
    virtual bool SetVFlip(bool enabled) = 0;
    virtual bool SetSwapBytes(bool enabled) { return false; }  // Optional, default no-op
    virtual std::string Explain(const std::string& question) = 0;
    // 取最后一帧（Capture 后有效）；未实现则返回 false，out 不修改
    virtual bool GetLastFrame(CameraFrame* out) { (void)out; return false; }
    // Capture() 内是否显示预览（分配 640×480 RGB565 并 SetPreviewImage）。app_ai 用人脸管道单独预览时可设为 false，避免双重预览抢 PSRAM。
    virtual void SetCapturePreviewEnabled(bool enabled) { (void)enabled; }
};

#endif // CAMERA_H
