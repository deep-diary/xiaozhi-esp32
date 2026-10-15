#include "config_center/config_api_client.h"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

#include <cstdio>
#include <cstring>

#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include <esp_crt_bundle.h>
#endif

#define TAG "cfg_api"

namespace config_center {

namespace {

constexpr int kHttpTimeoutMs = 15000;

/** 从完整 URL 提取 path（不含 scheme://host，含 query） */
std::string ExtractPath(const std::string& url) {
    const size_t scheme = url.find("://");
    if (scheme == std::string::npos) {
        return url;
    }
    const size_t host_start = scheme + 3;
    const size_t path_start = url.find('/', host_start);
    if (path_start == std::string::npos) {
        return "/";
    }
    return url.substr(path_start);
}

esp_err_t HttpEvent(esp_http_client_event_t* evt) {
    if (evt->event_id != HTTP_EVENT_ON_DATA || !evt->user_data || !evt->data || evt->data_len <= 0) {
        return ESP_OK;
    }
    auto* out = static_cast<std::string*>(evt->user_data);
    out->append(static_cast<const char*>(evt->data), static_cast<size_t>(evt->data_len));
    return ESP_OK;
}

std::string HexEncode(const unsigned char* data, size_t len) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(kHex[(data[i] >> 4) & 0xF]);
        out.push_back(kHex[data[i] & 0xF]);
    }
    return out;
}

/** hex 字符串 → 字节；失败返回空 */
bool HexDecode(const std::string& hex, std::string* out) {
    if (hex.size() % 2 != 0) {
        return false;
    }
    out->clear();
    out->reserve(hex.size() / 2);
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out->push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
}

}  // namespace

ConfigApiClient::ConfigApiClient(Endpoint ep) : ep_(std::move(ep)) {}

std::string ConfigApiClient::BodySha256Hex(const std::string& body) {
    unsigned char digest[32] = {};
    mbedtls_sha256(reinterpret_cast<const unsigned char*>(body.data()), body.size(), digest, 0);
    return HexEncode(digest, sizeof(digest));
}

bool ConfigApiClient::SignRequest(const std::string& method, const std::string& path,
                                  const std::string& body, std::string* out_signature,
                                  std::string* out_timestamp) const {
    std::string key;
    if (!HexDecode(ep_.device_secret, &key) || key.empty()) {
        return false;
    }
    const std::string timestamp = std::to_string(static_cast<long long>(esp_timer_get_time() / 1000000));
    const std::string base_string =
        method + "\n" + path + "\n" + timestamp + "\n" + BodySha256Hex(body);

    unsigned char mac[32] = {};
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) {
        return false;
    }
    if (mbedtls_md_hmac(info, reinterpret_cast<const unsigned char*>(key.data()), key.size(),
                        reinterpret_cast<const unsigned char*>(base_string.data()),
                        base_string.size(), mac) != 0) {
        return false;
    }

    unsigned char b64[64] = {};
    size_t b64_len = 0;
    if (mbedtls_base64_encode(b64, sizeof(b64), &b64_len, mac, sizeof(mac)) != 0) {
        return false;
    }
    *out_signature = std::string(reinterpret_cast<char*>(b64), b64_len);
    *out_timestamp = timestamp;
    return true;
}

int ConfigApiClient::PostJson(const std::string& url_path, const std::string& body,
                              std::string* resp, bool sign) {
    if (resp) {
        resp->clear();
    }
    std::string base = ep_.base_url;
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    if (base.empty()) {
        ESP_LOGW(TAG, "base_url empty");
        return -1;
    }
    const std::string url = base + url_path;

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = kHttpTimeoutMs;
    cfg.event_handler = HttpEvent;
    cfg.user_data = resp;
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    if (url.rfind("https://", 0) == 0) {
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
    }
#endif
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return -1;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (!ep_.device_id.empty()) {
        esp_http_client_set_header(client, "X-Device-Id", ep_.device_id.c_str());
    }
    if (sign) {
        std::string sig;
        std::string ts;
        if (SignRequest("POST", ExtractPath(url), body, &sig, &ts)) {
            esp_http_client_set_header(client, "X-Device-Timestamp", ts.c_str());
            esp_http_client_set_header(client, "X-Device-Signature", sig.c_str());
        } else {
            ESP_LOGW(TAG, "sign failed (no secret?); request unsigned");
        }
    }
    if (!body.empty()) {
        esp_http_client_set_post_field(client, body.c_str(), static_cast<int>(body.size()));
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = -1;
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
    } else {
        ESP_LOGW(TAG, "http %s failed: %s", url.c_str(), esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return status;
}

bool ConfigApiClient::Register(const std::string& device_type, const std::string& hardware_id,
                               const std::string& firmware_version, const std::string& name,
                               std::string* out_secret, int* out_version) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", ep_.device_id.c_str());
    cJSON_AddStringToObject(root, "device_type", device_type.c_str());
    cJSON_AddStringToObject(root, "hardware_id", hardware_id.c_str());
    cJSON_AddStringToObject(root, "firmware_version", firmware_version.c_str());
    cJSON_AddStringToObject(root, "name", name.c_str());
    char* body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        return false;
    }
    const std::string body_str(body);
    cJSON_free(body);

    std::string resp;
    const int status = PostJson("/devices/register", body_str, &resp, /*sign=*/false);
    if (status != 200) {
        ESP_LOGW(TAG, "register failed status=%d", status);
        return false;
    }
    cJSON* r = cJSON_Parse(resp.c_str());
    if (!r) {
        ESP_LOGW(TAG, "register bad json");
        return false;
    }
    const cJSON* secret = cJSON_GetObjectItemCaseSensitive(r, "device_secret");
    if (cJSON_IsString(secret) && secret->valuestring) {
        if (out_secret) {
            *out_secret = secret->valuestring;
        }
    }
    const cJSON* ver = cJSON_GetObjectItemCaseSensitive(r, "config_version");
    if (cJSON_IsNumber(ver) && out_version) {
        *out_version = ver->valueint;
    }
    cJSON_Delete(r);
    return true;
}

PullResponse ConfigApiClient::Pull(int current_version, const std::string& current_fingerprint,
                                   const std::vector<std::string>& schema_versions) {
    PullResponse out;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "current_config_version", current_version);
    if (!current_fingerprint.empty()) {
        cJSON_AddStringToObject(root, "current_fingerprint", current_fingerprint.c_str());
    }
    cJSON* schemas = cJSON_AddArrayToObject(root, "schema_versions");
    for (const auto& s : schema_versions) {
        cJSON_AddItemToArray(schemas, cJSON_CreateString(s.c_str()));
    }
    char* body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        return out;
    }
    const std::string body_str(body);
    cJSON_free(body);

    std::string resp;
    const int status = PostJson("/devices/config/pull", body_str, &resp, /*sign=*/true);
    out.http_status = status;
    out.raw_json = resp;

    if (status == 200) {
        cJSON* r = cJSON_Parse(resp.c_str());
        if (!r) {
            ESP_LOGW(TAG, "pull bad json");
            out.result = PullResult::kError;
            return out;
        }
        const cJSON* cfg = cJSON_GetObjectItemCaseSensitive(r, "config");
        out.result = cJSON_IsObject(cfg) ? PullResult::kUpdated : PullResult::kNotModified;
        cJSON_Delete(r);
        return out;
    }

    // 错误：解析 error_code
    cJSON* r = cJSON_Parse(resp.c_str());
    if (r) {
        const cJSON* ec = cJSON_GetObjectItemCaseSensitive(r, "error_code");
        if (cJSON_IsString(ec) && ec->valuestring) {
            out.error_code = ec->valuestring;
        }
        cJSON_Delete(r);
    }
    ESP_LOGW(TAG, "pull failed status=%d code=%s", status, out.error_code.c_str());
    out.result = PullResult::kError;
    return out;
}

AckResponse ConfigApiClient::Ack(int config_version, const std::string& status,
                                 const std::vector<std::string>& errors) {
    AckResponse out;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "config_version", config_version);
    cJSON_AddStringToObject(root, "status", status.c_str());
    cJSON* errs = cJSON_AddArrayToObject(root, "errors");
    for (const auto& e : errors) {
        cJSON_AddItemToArray(errs, cJSON_CreateString(e.c_str()));
    }
    char* body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        return out;
    }
    const std::string body_str(body);
    cJSON_free(body);

    std::string resp;
    const int http_status = PostJson("/devices/config/ack", body_str, &resp, /*sign=*/true);
    out.http_status = http_status;
    out.ok = (http_status == 200);
    if (!out.ok) {
        ESP_LOGW(TAG, "ack failed status=%d", http_status);
    }
    return out;
}

}  // namespace config_center
