#pragma once

#include <string>
#include <vector>

#include "config_center/config_types.h"

namespace config_center {

/** pull 结果 */
enum class PullResult {
    kUpdated,      ///< 有新配置，raw_json 为完整响应体
    kNotModified,  ///< 版本相同（服务器返回简化响应）
    kError,        ///< 网络 / 服务器 / 解析错误
};

struct PullResponse {
    PullResult result = PullResult::kError;
    int http_status = -1;
    std::string raw_json;  ///< 完整响应体（kUpdated 时含 config）
    std::string error_code;  ///< CFG_xxx
};

struct AckResponse {
    bool ok = false;
    int http_status = -1;
};

/**
 * 配置中心 HTTP API 客户端。
 *
 * 对齐 deep-trace 设备端接口：
 *   POST {base}/devices/register
 *   POST {base}/devices/config/pull    （HMAC）
 *   POST {base}/devices/config/ack     （HMAC）
 *
 * HMAC-SHA256 签名（见 deep-trace devices/config/auth.py）：
 *   base_string = "{METHOD}\n{path}\n{timestamp}\n{body_sha256_hex}"
 *   key         = hex_decode(device_secret)
 *   header      = X-Device-Signature: base64(hmac_sha256(key, base_string))
 *   path        = URL path（不含 host/query），如 "/api/v1/devices/config/pull"
 */
class ConfigApiClient {
public:
    struct Endpoint {
        std::string base_url;       ///< 如 "https://host/api/v1"（不含尾斜杠）
        std::string device_id;
        std::string device_secret;  ///< hex 字符串；空则不签名（开发降级）
    };

    explicit ConfigApiClient(Endpoint ep);

    void SetDeviceSecret(const std::string& secret) { ep_.device_secret = secret; }

    /** 注册设备：成功后 out_secret 填充 device_secret，out_version 填充当前版本 */
    bool Register(const std::string& device_type, const std::string& hardware_id,
                  const std::string& firmware_version, const std::string& name,
                  std::string* out_secret, int* out_version);

    /** 拉取配置 */
    PullResponse Pull(int current_version, const std::string& current_fingerprint,
                      const std::vector<std::string>& schema_versions);

    /** 上报生效结果；errors 为 `{field, code, message}` 对象数组（CFG-01 §4.4） */
    AckResponse Ack(int config_version, const std::string& status,
                    const std::vector<ConfigApplyError>& errors);

private:
    /** POST JSON；返回 HTTP 状态码（-1=网络错误）。resp 收响应体。 */
    int PostJson(const std::string& url_path, const std::string& body, std::string* resp,
                 bool sign);

    /** 计算请求体 SHA256 hex */
    static std::string BodySha256Hex(const std::string& body);

    /** 生成签名并写入请求头 */
    bool SignRequest(const std::string& method, const std::string& path, const std::string& body,
                     std::string* out_signature, std::string* out_timestamp) const;

    Endpoint ep_;
};

}  // namespace config_center
