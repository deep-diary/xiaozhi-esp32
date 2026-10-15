#include "pairing/deep_dog_pair_http.h"

#include "mqtt/mqtt_config.h"
#include "settings.h"

#include "application.h"
#include "assets/lang_config.h"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include <esp_crt_bundle.h>
#endif

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#define TAG "dog_pair_http"

namespace {

/** 轮询间隔（µs）：3s 起，网络错误/429 时指数退避至 30s */
constexpr int64_t kPollStartUs = 3LL * 1000 * 1000;
constexpr int64_t kPollMaxUs = 30LL * 1000 * 1000;
/** 激活轮询默认硬超时（handshake.activation.timeout_ms 缺失时） */
constexpr int kDefaultTimeoutMs = 120 * 1000;
constexpr int kHandshakeMaxAttempts = 3;
constexpr int kHandshakeBackoffMs = 30 * 1000;
constexpr int kActivateMaxErrors = 3;
constexpr uint32_t kTaskStack = 8192;

std::string MacCompact() {
    return DeepDogMqttConfig::MacCompactDeviceId();
}

std::string MacDisplay() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
    return buf;
}

void TrimTrailingSlash(std::string* s) {
    while (!s->empty() && s->back() == '/') {
        s->pop_back();
    }
}

/** 回环/未指定 host 对设备无意义（服务端视角的 127.0.0.1），不覆盖本地 broker */
bool IsLoopbackHost(const std::string& host) {
    return host.empty() || host == "127.0.0.1" || host == "localhost" || host == "0.0.0.0" ||
           host.rfind("127.", 0) == 0;
}

std::string FirmwareVersion() {
    const esp_app_desc_t* app = esp_app_get_description();
    return (app && app->version[0] != '\0') ? app->version : "0.0.0";
}

/** 可中断睡眠；返回 false 表示已请求取消 */
bool SleepInterruptible(const std::atomic<bool>& cancel, int total_ms) {
    const int slice = 200;
    for (int elapsed = 0; elapsed < total_ms; elapsed += slice) {
        if (cancel.load(std::memory_order_acquire)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(slice));
    }
    return !cancel.load(std::memory_order_acquire);
}

esp_err_t HttpEvent(esp_http_client_event_t* evt) {
    if (evt->event_id != HTTP_EVENT_ON_DATA || !evt->user_data || !evt->data || evt->data_len <= 0) {
        return ESP_OK;
    }
    auto* out = static_cast<std::string*>(evt->user_data);
    out->append(static_cast<const char*>(evt->data), static_cast<size_t>(evt->data_len));
    return ESP_OK;
}

/** POST JSON；返回 HTTP 状态码（-1=网络/客户端错误），resp 收响应体 */
int HttpPostJson(const std::string& url, const std::string& device_id, const std::string& body,
                 std::string* resp) {
    if (resp) {
        resp->clear();
    }
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 15000;
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
    if (!device_id.empty()) {
        esp_http_client_set_header(client, "Device-Id", device_id.c_str());
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

std::string JsonString(const cJSON* obj, const char* key) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        return item->valuestring;
    }
    return "";
}

/** 从 deep-trace/{home}/{wu}/telemetry 反推 home / work_unit */
void DeriveFromTelemetry(const std::string& topic, std::string* home, std::string* wu) {
    if (topic.empty()) {
        return;
    }
    const size_t p1 = topic.find('/');
    if (p1 == std::string::npos) {
        return;
    }
    const size_t p2 = topic.find('/', p1 + 1);
    if (p2 == std::string::npos) {
        return;
    }
    const size_t p3 = topic.find('/', p2 + 1);
    if (home) {
        *home = topic.substr(p1 + 1, p2 - p1 - 1);
    }
    if (wu) {
        *wu = (p3 == std::string::npos) ? topic.substr(p2 + 1) : topic.substr(p2 + 1, p3 - p2 - 1);
    }
}

}  // namespace

DeepDogPairHttp::DeepDogPairHttp() = default;

DeepDogPairHttp::~DeepDogPairHttp() {
    Cancel();
    // 等待任务退出，避免析构后任务仍访问本对象
    for (int i = 0; i < 50 && running_.load(std::memory_order_acquire); ++i) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void DeepDogPairHttp::SetIdentityReloadCallback(std::function<void()> cb) {
    identity_reload_cb_ = std::move(cb);
}

void DeepDogPairHttp::Start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        ESP_LOGW(TAG, "http pairing already running");
        return;
    }
    cancel_.store(false, std::memory_order_release);
    if (xTaskCreate(&DeepDogPairHttp::TaskEntry, "dog_pair_http", kTaskStack, this, 4, nullptr) != pdPASS) {
        running_.store(false, std::memory_order_release);
        ESP_LOGE(TAG, "failed to create pairing task");
    }
}

void DeepDogPairHttp::Cancel() {
    cancel_.store(true, std::memory_order_release);
}

void DeepDogPairHttp::TaskEntry(void* arg) {
    auto* self = static_cast<DeepDogPairHttp*>(arg);
    if (self) {
        self->Run();
        self->running_.store(false, std::memory_order_release);
    }
    vTaskDelete(nullptr);
}

void DeepDogPairHttp::AnnounceCode(const std::string& code) {
    if (code.empty()) {
        return;
    }
    struct DigitSound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<DigitSound, 10> digit_sounds{{
        {'0', Lang::Sounds::OGG_0}, {'1', Lang::Sounds::OGG_1}, {'2', Lang::Sounds::OGG_2},
        {'3', Lang::Sounds::OGG_3}, {'4', Lang::Sounds::OGG_4}, {'5', Lang::Sounds::OGG_5},
        {'6', Lang::Sounds::OGG_6}, {'7', Lang::Sounds::OGG_7}, {'8', Lang::Sounds::OGG_8},
        {'9', Lang::Sounds::OGG_9},
    }};
    auto& app = Application::GetInstance();
    for (char digit : code) {
        for (const auto& ds : digit_sounds) {
            if (ds.digit == digit) {
                app.PlaySound(ds.sound);
                break;
            }
        }
    }
}

bool DeepDogPairHttp::ApplyBoundJson(const char* json) {
    cJSON* root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(TAG, "activate 200 bad json");
        return false;
    }

    const std::string device_id = MacCompact();
    std::string broker_host;
    int broker_port = 0;
    bool use_tls = false;
    cJSON* mqtt = const_cast<cJSON*>(cJSON_GetObjectItemCaseSensitive(root, "mqtt"));
    if (cJSON_IsObject(mqtt)) {
        broker_host = JsonString(mqtt, "host");
        const cJSON* port = cJSON_GetObjectItemCaseSensitive(mqtt, "port");
        if (cJSON_IsNumber(port)) {
            broker_port = port->valueint;
        }
        const cJSON* tls = cJSON_GetObjectItemCaseSensitive(mqtt, "tls");
        if (cJSON_IsBool(tls)) {
            use_tls = cJSON_IsTrue(tls);
        }
    }

    std::string telemetry = JsonString(root, "telemetry_topic");
    cJSON* topics = const_cast<cJSON*>(cJSON_GetObjectItemCaseSensitive(root, "topics"));
    if (cJSON_IsObject(topics)) {
        const cJSON* t = cJSON_GetObjectItemCaseSensitive(topics, "telemetry");
        if (cJSON_IsString(t) && t->valuestring) {
            telemetry = t->valuestring;
        } else if (cJSON_IsObject(t)) {
            telemetry = JsonString(t, "topic");
        }
        if (telemetry.empty()) {
            const cJSON* tt = cJSON_GetObjectItemCaseSensitive(topics, "telemetry_topic");
            if (cJSON_IsString(tt) && tt->valuestring) {
                telemetry = tt->valuestring;
            }
        }
    }

    std::string home = JsonString(root, "home_code");
    std::string wu = JsonString(root, "work_unit_code");
    if (home.empty() || wu.empty()) {
        DeriveFromTelemetry(telemetry, &home, &wu);
    }
    // 服务端可能下发服务器视角的回环 broker；对设备无意义，保留本地配置
    if (IsLoopbackHost(broker_host)) {
        ESP_LOGW(TAG, "handshake broker host '%s' is loopback; keep local broker", broker_host.c_str());
        broker_host.clear();
    }

    // 落盘：绑定态 + 业务 Broker / 遥测配置
    {
        Settings st("deep_dog_mqtt", true);
        st.SetBool("bound", true);
        st.SetString("device_id", device_id);
        st.EraseKey("pair_code");
    }
    DeepDogMqttConfig::SaveBoundEndpoint(broker_host, broker_port, use_tls, telemetry, home, wu);

    ESP_LOGI(TAG, "http pairing bound device_id=%s broker=%s:%d tls=%d telemetry=%s", device_id.c_str(),
             broker_host.c_str(), broker_port, use_tls ? 1 : 0, telemetry.c_str());

    // 不可在配对任务里 Stop/Start MQTT：defer 到主循环
    Application::GetInstance().Schedule([this]() {
        if (identity_reload_cb_) {
            identity_reload_cb_();
        }
        Application::GetInstance().Alert("配对", "绑定成功", "happy", Lang::Sounds::OGG_SUCCESS);
    });
    cJSON_Delete(root);
    return true;
}

void DeepDogPairHttp::Run() {
    const DeepDogMqttSettings s = DeepDogMqttConfig::Load();
    std::string base = s.server_api_base;
    TrimTrailingSlash(&base);
    if (base.empty()) {
        ESP_LOGW(TAG, "server_api_base empty; HTTP pairing skipped");
        Application::GetInstance().Alert("配对", "未配置服务器地址", "sad", std::string_view{});
        return;
    }

    const std::string mac = MacCompact();
    char body[160];
    snprintf(body, sizeof(body), "{\"mac\":\"%s\",\"fw\":\"%s\"}", MacDisplay().c_str(),
             FirmwareVersion().c_str());

    // ---- 握手（429 退避重试） ----
    std::string code;
    std::string challenge;
    int timeout_ms = kDefaultTimeoutMs;
    bool awaiting = false;
    for (int attempt = 1; attempt <= kHandshakeMaxAttempts; ++attempt) {
        if (cancel_.load(std::memory_order_acquire)) {
            return;
        }
        std::string resp;
        const int status = HttpPostJson(base + "/devices/pair/handshake", mac, body, &resp);
        if (status == 200) {
            cJSON* root = cJSON_Parse(resp.c_str());
            if (!root) {
                ESP_LOGW(TAG, "handshake 200 bad json");
                Application::GetInstance().Alert("配对", "服务器响应异常", "sad", std::string_view{});
                return;
            }
            const cJSON* bound = cJSON_GetObjectItemCaseSensitive(root, "bound");
            const bool is_bound = cJSON_IsBool(bound) && cJSON_IsTrue(bound);
            if (is_bound) {
                const bool ok = ApplyBoundJson(resp.c_str());
                cJSON_Delete(root);
                (void)ok;
                return;
            }
            cJSON* activation = const_cast<cJSON*>(cJSON_GetObjectItemCaseSensitive(root, "activation"));
            if (cJSON_IsObject(activation)) {
                code = JsonString(activation, "code");
                challenge = JsonString(activation, "challenge");
                const cJSON* tmo = cJSON_GetObjectItemCaseSensitive(activation, "timeout_ms");
                if (cJSON_IsNumber(tmo) && tmo->valueint > 0) {
                    timeout_ms = tmo->valueint;
                }
            }
            cJSON_Delete(root);
            awaiting = !code.empty();
            break;
        }
        if (status == 429) {
            ESP_LOGW(TAG, "handshake 429 rate limited (attempt %d/%d)", attempt, kHandshakeMaxAttempts);
            if (attempt >= kHandshakeMaxAttempts || !SleepInterruptible(cancel_, kHandshakeBackoffMs)) {
                Application::GetInstance().Alert("配对", "服务器繁忙，请稍后重试", "sad", std::string_view{});
                return;
            }
            continue;
        }
        ESP_LOGW(TAG, "handshake failed status=%d", status);
        Application::GetInstance().Alert("配对", "无法连接服务器", "sad", std::string_view{});
        return;
    }
    if (!awaiting) {
        ESP_LOGW(TAG, "handshake no activation code");
        Application::GetInstance().Alert("配对", "未获取到配对码", "sad", std::string_view{});
        return;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "请在网页输入配对码：%s", code.c_str());
    Application::GetInstance().Alert("配对", msg, "link", Lang::Sounds::OGG_POPUP);
    AnnounceCode(code);

    // ---- 轮询确认 ----
    char act_body[512];
    if (challenge.empty()) {
        snprintf(act_body, sizeof(act_body), "{\"device_id\":\"%s\"}", mac.c_str());
    } else {
        // 联调：服务端未启用 HMAC 时直接放行；hmac 字段留待完整鉴权补齐
        snprintf(act_body, sizeof(act_body), "{\"device_id\":\"%s\",\"challenge\":\"%s\"}", mac.c_str(),
                 challenge.c_str());
    }
    const int64_t deadline_us = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
    int64_t interval_us = kPollStartUs;
    int error_count = 0;

    while (!cancel_.load(std::memory_order_acquire)) {
        if (esp_timer_get_time() >= deadline_us) {
            ESP_LOGW(TAG, "http pairing timed out");
            Application::GetInstance().Alert("配对", "配对超时，请重试", "neutral", std::string_view{});
            return;
        }
        if (!SleepInterruptible(cancel_, static_cast<int>(interval_us / 1000))) {
            return;
        }

        std::string resp;
        const int status = HttpPostJson(base + "/devices/pair/activate", mac, act_body, &resp);
        switch (status) {
            case 200:
                ApplyBoundJson(resp.c_str());
                return;
            case 202: {
                error_count = 0;
                interval_us = kPollStartUs;
                cJSON* root = cJSON_Parse(resp.c_str());
                if (root) {
                    const cJSON* exp = cJSON_GetObjectItemCaseSensitive(root, "expires_in");
                    if (cJSON_IsNumber(exp) && exp->valueint > 0) {
                        ESP_LOGD(TAG, "pairing pending expires_in=%d", exp->valueint);
                    }
                    cJSON_Delete(root);
                }
                break;
            }
            case 404:
            case 410:
                // 终止态：未握手 / 会话过期；立即停止，回 idle
                ESP_LOGW(TAG, "http pairing terminal status=%d", status);
                Application::GetInstance().Alert("配对", "配对码已失效，请重试", "neutral", std::string_view{});
                return;
            case 401:
                ESP_LOGW(TAG, "http pairing auth failed (401)");
                Application::GetInstance().Alert("配对", "配对鉴权失败", "sad", std::string_view{});
                return;
            case 429:
                interval_us = (interval_us * 2 > kPollMaxUs) ? kPollMaxUs : interval_us * 2;
                ESP_LOGW(TAG, "activate 429; backoff to %lld s", (long long)(interval_us / 1000000));
                break;
            default:
                error_count++;
                ESP_LOGW(TAG, "activate error status=%d (%d/%d)", status, error_count, kActivateMaxErrors);
                if (error_count >= kActivateMaxErrors) {
                    Application::GetInstance().Alert("配对", "网络异常，配对终止", "sad", std::string_view{});
                    return;
                }
                interval_us = (interval_us * 2 > kPollMaxUs) ? kPollMaxUs : interval_us * 2;
                break;
        }
    }
}
