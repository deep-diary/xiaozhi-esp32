#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "vision_types.h"

/**
 * RTSP 客户端发布：TCP interleaved + RTP/H.264（RFC 6184）→ MediaMTX。
 * 握手：OPTIONS → ANNOUNCE → SETUP → RECORD（带 Session）。
 */
class RtspH264Pusher {
public:
    RtspH264Pusher() = default;
    ~RtspH264Pusher();

    void SetUrl(const std::string& url);
    std::string Url() const;

    bool Connect();
    void Disconnect();
    bool IsConnected() const { return sock_ >= 0; }

    /** 推一帧 Annex-B（可含多 NAL）；失败时关闭套接字 */
    bool PushAnnexB(const uint8_t* data, size_t len);

    VisionPushStatus Status() const {
        return static_cast<VisionPushStatus>(status_.load(std::memory_order_acquire));
    }

private:
    bool ParseUrl(const std::string& url, std::string* host, uint16_t* port, std::string* path) const;
    bool SendAll(const void* data, size_t len);
    bool RecvResponse(int* status_code, std::string* headers_out, std::string* body);
    bool SendRtsp(const std::string& method, const std::string& url_full, const std::string& extra_headers,
                  const std::string& body, std::string* headers_out);
    void MaybeCaptureSession(const std::string& headers);
    bool SendInterleavedRtp(const uint8_t* rtp, size_t rtp_len);
    bool SendOneNal(const uint8_t* nal, size_t nal_len, bool marker);

    mutable std::mutex mu_;
    std::string url_;
    std::string session_;
    int sock_ = -1;
    int cseq_ = 1;
    uint16_t seq_ = 0;
    uint32_t timestamp_ = 0;
    uint32_t ssrc_ = 0x48323634;  // 'H264'
    std::atomic<uint8_t> status_{static_cast<uint8_t>(VisionPushStatus::Idle)};
};
