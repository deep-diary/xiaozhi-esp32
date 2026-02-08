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
    // 只采集不显示：DQBUF + 填 frame_ + QBUF 等，不执行任何预览；未实现则返回 false
    virtual bool CaptureOnly() { return false; }
    // 将当前内部 frame_ 显示到屏幕；未实现则返回 false
    virtual bool ShowLastFrame() { return false; }
    virtual bool SetHMirror(bool enabled) = 0;
    virtual bool SetVFlip(bool enabled) = 0;
    virtual bool SetSwapBytes(bool enabled) { return false; }  // Optional, default no-op
    virtual std::string Explain(const std::string& question) = 0;
    // 取最后一帧（Capture/CaptureOnly 后有效）；未实现则返回 false，out 不修改
    virtual bool GetLastFrame(CameraFrame* out) { (void)out; return false; }
};

#endif // CAMERA_H
