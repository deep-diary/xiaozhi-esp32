#include "vision/rtsp_jpeg_pusher.h"

#include "vision/vision_config.h"

#include <esp_log.h>
#include <esp_random.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netdb.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#define TAG "rtsp_push"

namespace {

constexpr size_t kMaxJpegRtp = 60000;

static void WriteBe16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>((v >> 8) & 0xff);
    p[1] = static_cast<uint8_t>(v & 0xff);
}

static void WriteBe24(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 16) & 0xff);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
    p[2] = static_cast<uint8_t>(v & 0xff);
}

static void WriteBe32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xff);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xff);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xff);
    p[3] = static_cast<uint8_t>(v & 0xff);
}

static std::string ExtractHeaderValue(const std::string& headers, const char* key) {
    const char* p = strcasestr(headers.c_str(), key);
    if (!p) {
        return {};
    }
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == ':') {
        ++p;
    }
    const char* end = strstr(p, "\r\n");
    if (!end) {
        end = p + strlen(p);
    }
    std::string v(p, end);
    // Session 可能带 ;timeout=
    const size_t semi = v.find(';');
    if (semi != std::string::npos) {
        v = v.substr(0, semi);
    }
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) {
        v.pop_back();
    }
    return v;
}

}  // namespace

RtspJpegPusher::~RtspJpegPusher() {
    Disconnect();
}

void RtspJpegPusher::SetUrl(const std::string& url) {
    std::lock_guard<std::mutex> lock(mu_);
    url_ = url;
}

std::string RtspJpegPusher::Url() const {
    std::lock_guard<std::mutex> lock(mu_);
    return url_;
}

bool RtspJpegPusher::ParseUrl(const std::string& url, std::string* host, uint16_t* port, std::string* path) const {
    if (!host || !port || !path) {
        return false;
    }
    const char* p = url.c_str();
    if (strncmp(p, "rtsp://", 7) != 0) {
        return false;
    }
    p += 7;
    const char* slash = strchr(p, '/');
    std::string authority = slash ? std::string(p, slash) : std::string(p);
    *path = slash ? std::string(slash) : std::string("/");
    if (path->empty() || (*path)[0] != '/') {
        *path = "/" + *path;
    }

    const char* colon = strchr(authority.c_str(), ':');
    if (colon) {
        *host = std::string(authority.c_str(), colon);
        *port = static_cast<uint16_t>(atoi(colon + 1));
        if (*port == 0) {
            *port = DEEP_DOG_VISION_RTSP_PORT;
        }
    } else {
        *host = authority;
        *port = DEEP_DOG_VISION_RTSP_PORT;
    }
    return !host->empty();
}

bool RtspJpegPusher::SendAll(const void* data, size_t len) {
    if (sock_ < 0 || !data || len == 0) {
        return false;
    }
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t left = len;
    while (left > 0) {
        const ssize_t n = ::send(sock_, p, left, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            ESP_LOGW(TAG, "send failed errno=%d", errno);
            return false;
        }
        if (n == 0) {
            return false;
        }
        p += static_cast<size_t>(n);
        left -= static_cast<size_t>(n);
    }
    return true;
}

void RtspJpegPusher::MaybeCaptureSession(const std::string& headers) {
    const std::string sid = ExtractHeaderValue(headers, "Session");
    if (!sid.empty()) {
        session_ = sid;
        ESP_LOGI(TAG, "RTSP Session=%s", session_.c_str());
    }
}

bool RtspJpegPusher::RecvResponse(int* status_code, std::string* headers_out, std::string* body) {
    if (sock_ < 0 || !status_code) {
        return false;
    }
    std::string raw;
    raw.reserve(1024);
    char buf[256];
    for (;;) {
        const ssize_t n = ::recv(sock_, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        raw.append(buf, static_cast<size_t>(n));
        if (raw.find("\r\n\r\n") != std::string::npos) {
            break;
        }
        if (raw.size() > 8192) {
            return false;
        }
    }

    const size_t hdr_end = raw.find("\r\n\r\n");
    const std::string headers = raw.substr(0, hdr_end);
    size_t content_len = 0;
    const char* cl = strcasestr(headers.c_str(), "Content-Length:");
    if (cl) {
        content_len = static_cast<size_t>(atoi(cl + 15));
    }
    std::string payload = raw.substr(hdr_end + 4);
    while (payload.size() < content_len) {
        const size_t need = content_len - payload.size();
        const size_t chunk = need > sizeof(buf) ? sizeof(buf) : need;
        const ssize_t n = ::recv(sock_, buf, chunk, 0);
        if (n <= 0) {
            return false;
        }
        payload.append(buf, static_cast<size_t>(n));
    }
    if (body) {
        *body = std::move(payload);
    }
    if (headers_out) {
        *headers_out = headers;
    }

    int code = 0;
    if (sscanf(headers.c_str(), "RTSP/%*s %d", &code) != 1) {
        return false;
    }
    *status_code = code;
    MaybeCaptureSession(headers);
    return true;
}

bool RtspJpegPusher::SendRtsp(const std::string& method, const std::string& url_full, const std::string& extra_headers,
                              const std::string& body, std::string* headers_out) {
    char session_hdr[96] = "";
    if (!session_.empty()) {
        snprintf(session_hdr, sizeof(session_hdr), "Session: %s\r\n", session_.c_str());
    }

    char req[2048];
    const int n = snprintf(req, sizeof(req),
                           "%s %s RTSP/1.0\r\n"
                           "CSeq: %d\r\n"
                           "User-Agent: deep-dog/vision\r\n"
                           "%s"
                           "%s"
                           "Content-Length: %u\r\n"
                           "\r\n"
                           "%s",
                           method.c_str(), url_full.c_str(), cseq_++, session_hdr, extra_headers.c_str(),
                           static_cast<unsigned>(body.size()), body.c_str());
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(req)) {
        return false;
    }
    if (!SendAll(req, static_cast<size_t>(n))) {
        return false;
    }
    int code = 0;
    std::string hdrs;
    if (!RecvResponse(&code, &hdrs, nullptr)) {
        ESP_LOGW(TAG, "%s: no response", method.c_str());
        return false;
    }
    if (headers_out) {
        *headers_out = hdrs;
    }
    if (code < 200 || code >= 300) {
        // 打一行响应首行方便排障
        const size_t nl = hdrs.find("\r\n");
        ESP_LOGW(TAG, "%s -> %.*s", method.c_str(), (int)(nl == std::string::npos ? hdrs.size() : nl), hdrs.c_str());
        return false;
    }
    return true;
}

bool RtspJpegPusher::SendInterleavedRtp(const uint8_t* rtp, size_t rtp_len) {
    if (rtp_len > 0xffff) {
        return false;
    }
    uint8_t hdr[4];
    hdr[0] = 0x24;
    hdr[1] = 0;
    WriteBe16(hdr + 2, static_cast<uint16_t>(rtp_len));
    if (!SendAll(hdr, 4)) {
        return false;
    }
    return SendAll(rtp, rtp_len);
}

bool RtspJpegPusher::Connect() {
    Disconnect();
    status_.store(static_cast<uint8_t>(VisionPushStatus::Starting), std::memory_order_release);
    session_.clear();

    std::string url_copy;
    {
        std::lock_guard<std::mutex> lock(mu_);
        url_copy = url_;
    }
    if (url_copy.empty()) {
        char built[160];
        snprintf(built, sizeof(built), "rtsp://%s:%u/%s", DEEP_DOG_VISION_RTSP_HOST,
                 static_cast<unsigned>(DEEP_DOG_VISION_RTSP_PORT), DEEP_DOG_VISION_STREAM_PATH);
        url_copy = built;
        std::lock_guard<std::mutex> lock(mu_);
        url_ = url_copy;
    }

    std::string host;
    uint16_t port = DEEP_DOG_VISION_RTSP_PORT;
    std::string path;
    if (!ParseUrl(url_copy, &host, &port, &path)) {
        ESP_LOGE(TAG, "bad url: %s", url_copy.c_str());
        status_.store(static_cast<uint8_t>(VisionPushStatus::Error), std::memory_order_release);
        return false;
    }

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", static_cast<unsigned>(port));
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    const int gai = getaddrinfo(host.c_str(), port_str, &hints, &res);
    if (gai != 0 || !res) {
        ESP_LOGW(TAG, "getaddrinfo %s failed %d", host.c_str(), gai);
        status_.store(static_cast<uint8_t>(VisionPushStatus::Error), std::memory_order_release);
        return false;
    }

    sock_ = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock_ < 0) {
        freeaddrinfo(res);
        status_.store(static_cast<uint8_t>(VisionPushStatus::Error), std::memory_order_release);
        return false;
    }
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (::connect(sock_, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGW(TAG, "connect %s:%u failed errno=%d", host.c_str(), static_cast<unsigned>(port), errno);
        freeaddrinfo(res);
        ::close(sock_);
        sock_ = -1;
        status_.store(static_cast<uint8_t>(VisionPushStatus::Error), std::memory_order_release);
        return false;
    }
    freeaddrinfo(res);

    cseq_ = 1;
    seq_ = static_cast<uint16_t>(esp_random() & 0xffff);
    timestamp_ = esp_random();
    ssrc_ = 0x444F4756;

    const std::string full = url_copy;
    // control 用相对 track，SETUP 拼到 base URL
    char sdp[384];
    snprintf(sdp, sizeof(sdp),
             "v=0\r\n"
             "o=- 0 0 IN IP4 0.0.0.0\r\n"
             "s=deep-dog\r\n"
             "c=IN IP4 0.0.0.0\r\n"
             "t=0 0\r\n"
             "m=video 0 RTP/AVP 26\r\n"
             "a=rtpmap:26 JPEG/90000\r\n"
             "a=control:trackID=0\r\n");

    if (!SendRtsp("OPTIONS", full, "", "", nullptr)) {
        ::close(sock_);
        sock_ = -1;
        status_.store(static_cast<uint8_t>(VisionPushStatus::Error), std::memory_order_release);
        return false;
    }
    if (!SendRtsp("ANNOUNCE", full, "Content-Type: application/sdp\r\n", sdp, nullptr)) {
        ::close(sock_);
        sock_ = -1;
        status_.store(static_cast<uint8_t>(VisionPushStatus::Error), std::memory_order_release);
        return false;
    }

    const std::string setup_url = full + (full.back() == '/' ? "" : "/") + "trackID=0";
    const char* transport = "Transport: RTP/AVP/TCP;unicast;interleaved=0-1;mode=RECORD\r\n";
    if (!SendRtsp("SETUP", setup_url, transport, "", nullptr)) {
        // 回退：部分实现对 control URL 拼法不同
        if (!SendRtsp("SETUP", full, transport, "", nullptr)) {
            ::close(sock_);
            sock_ = -1;
            status_.store(static_cast<uint8_t>(VisionPushStatus::Error), std::memory_order_release);
            return false;
        }
    }
    if (session_.empty()) {
        ESP_LOGW(TAG, "SETUP ok but Session missing");
    }
    if (!SendRtsp("RECORD", full, "Range: npt=0.000-\r\n", "", nullptr)) {
        ::close(sock_);
        sock_ = -1;
        status_.store(static_cast<uint8_t>(VisionPushStatus::Error), std::memory_order_release);
        return false;
    }

    ESP_LOGI(TAG, "RTSP publish ok -> %s session=%s", full.c_str(), session_.c_str());
    status_.store(static_cast<uint8_t>(VisionPushStatus::Streaming), std::memory_order_release);
    return true;
}

void RtspJpegPusher::Disconnect() {
    if (sock_ >= 0) {
        ::close(sock_);
        sock_ = -1;
    }
    session_.clear();
    status_.store(static_cast<uint8_t>(VisionPushStatus::Idle), std::memory_order_release);
}

bool RtspJpegPusher::PushJpeg(const uint8_t* jpeg, size_t len, uint16_t width, uint16_t height) {
    if (!jpeg || len == 0 || sock_ < 0) {
        return false;
    }
    if (len + 20 > kMaxJpegRtp) {
        ESP_LOGW(TAG, "jpeg too large %u", static_cast<unsigned>(len));
        return false;
    }

    std::vector<uint8_t> pkt(12 + 8 + len);
    uint8_t* rtp = pkt.data();
    rtp[0] = 0x80;
    rtp[1] = 0x80 | 26;
    WriteBe16(rtp + 2, seq_++);
    WriteBe32(rtp + 4, timestamp_);
    WriteBe32(rtp + 8, ssrc_);

    uint8_t* jh = rtp + 12;
    jh[0] = 0;
    WriteBe24(jh + 1, 0);
    jh[4] = 1;
    jh[5] = 255;
    jh[6] = static_cast<uint8_t>((width + 7) / 8);
    jh[7] = static_cast<uint8_t>((height + 7) / 8);
    memcpy(jh + 8, jpeg, len);

    const uint32_t fps = DEEP_DOG_VISION_PUSH_FPS > 0 ? DEEP_DOG_VISION_PUSH_FPS : 5;
    timestamp_ += 90000 / fps;

    if (!SendInterleavedRtp(pkt.data(), pkt.size())) {
        status_.store(static_cast<uint8_t>(VisionPushStatus::Error), std::memory_order_release);
        if (sock_ >= 0) {
            ::close(sock_);
            sock_ = -1;
        }
        session_.clear();
        return false;
    }
    status_.store(static_cast<uint8_t>(VisionPushStatus::Streaming), std::memory_order_release);
    return true;
}
