#pragma once

#include <cstdint>

/** 视频发布形态：与 MJPEG Server / RTSP Client 互斥；Off 时仍可静默跑 face_ai */
enum class VisionPublishMode : uint8_t {
    Off = 0,
    HttpMjpeg = 1,
    RtspPush = 2,
};

enum class VisionPushStatus : uint8_t {
    Idle = 0,
    Starting = 1,
    Streaming = 2,
    Error = 3,
};

inline const char* VisionPublishModeStr(VisionPublishMode m) {
    switch (m) {
        case VisionPublishMode::Off:
            return "off";
        case VisionPublishMode::HttpMjpeg:
            return "mjpeg";
        case VisionPublishMode::RtspPush:
            return "rtsp_push";
        default:
            return "unknown";
    }
}

inline const char* VisionPushStatusStr(VisionPushStatus s) {
    switch (s) {
        case VisionPushStatus::Idle:
            return "idle";
        case VisionPushStatus::Starting:
            return "starting";
        case VisionPushStatus::Streaming:
            return "streaming";
        case VisionPushStatus::Error:
            return "error";
        default:
            return "unknown";
    }
}
