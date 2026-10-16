#include "config_center/config_manager.h"

#include "config_center/config_api_client.h"
#include "config_center/config_change_hub.h"
#include "ext_pin/ext_pin_mode.h"
#include "mqtt/mqtt_config.h"
#include "settings.h"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>
#include <cstring>
#include <mutex>

#define TAG "cfg_mgr"

namespace config_center {

namespace {

constexpr const char* kNvsNamespace = "deep_dog_cfg";
// MEM-03（S09）：实测稳态 stack_hwm=5992（2026-10-16，MQTT 已连、配置同步运行中），
// 8192→7168 回收 1KB internal，裁剪后仍留 ~1.1KB 余量（>512B 验收线）。
constexpr uint32_t kTaskStack = 7168;
constexpr int kPollTaskPriority = 4;
/** 失败重试退避（秒） */
constexpr int kRetryMinS = 30;
constexpr int kRetryMaxS = 300;

std::string FirmwareVersion() {
    const esp_app_desc_t* app = esp_app_get_description();
    return (app && app->version[0] != '\0') ? app->version : "0.0.0";
}

/** 从 cJSON 对象按点分路径取值（返回 cJSON*，无则 nullptr） */
const cJSON* JsonGetByPath(const cJSON* root, const std::string& path) {
    if (!root || path.empty()) {
        return nullptr;
    }
    const cJSON* cur = root;
    size_t start = 0;
    while (start < path.size()) {
        const size_t dot = path.find('.', start);
        const std::string key =
            (dot == std::string::npos) ? path.substr(start) : path.substr(start, dot - start);
        if (!cJSON_IsObject(cur)) {
            return nullptr;
        }
        cur = cJSON_GetObjectItemCaseSensitive(cur, key.c_str());
        if (!cur) {
            return nullptr;
        }
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }
    return cur;
}

std::string JsonStr(const cJSON* obj, const char* key, const std::string& fallback = "") {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        return item->valuestring;
    }
    return fallback;
}

int JsonInt(const cJSON* obj, const char* key, int fallback) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return fallback;
}

bool JsonBool(const cJSON* obj, const char* key, bool fallback) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return fallback;
}

void TrimTrailingSlash(std::string* s) {
    while (!s->empty() && s->back() == '/') {
        s->pop_back();
    }
}

/** 将 stream_path_template 中的 ${device_id} 替换为实际设备 ID */
std::string ResolveTemplate(const std::string& tmpl, const std::string& device_id) {
    const std::string key = "${device_id}";
    std::string out = tmpl;
    size_t pos = 0;
    while ((pos = out.find(key, pos)) != std::string::npos) {
        out.replace(pos, key.size(), device_id);
        pos += device_id.size();
    }
    return out;
}

}  // namespace

ConfigManager& ConfigManager::GetInstance() {
    static ConfigManager instance;
    return instance;
}

ConfigManager::~ConfigManager() { Stop(); }

// ──────────────────────────────────────────────────────────────
//  初始化
// ──────────────────────────────────────────────────────────────

void ConfigManager::Init() {
    // 设备 ID：MAC 紧凑小写串（与后端 device_id 对齐）
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char idbuf[13];
    snprintf(idbuf, sizeof(idbuf), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    device_id_ = idbuf;

    LoadFromNvs();
    if (config_.config_version == 0 && raw_config_json_.empty()) {
        // 无缓存 → 用旧 NVS / 默认值填充
        MigrateLegacyNvs();
    }

    ESP_LOGI(TAG, "init device_id=%s ver=%d api_base=%s secret=%s", device_id_.c_str(),
             config_.config_version, api_base_url_.empty() ? "(none)" : api_base_url_.c_str(),
             device_secret_.empty() ? "(none)" : "(set)");
}

void ConfigManager::LoadFromNvs() {
    Settings st(kNvsNamespace, false);
    raw_config_json_ = st.GetString("config_json", "");
    config_.config_version = st.GetInt("config_version", 0);
    fingerprint_ = st.GetString("fingerprint", "");
    device_secret_ = st.GetString("device_secret", "");
    api_base_url_ = st.GetString("api_base_url", "");
    last_pull_at_ = st.GetInt("last_pull_at", 0);

    if (!raw_config_json_.empty()) {
        if (ParseConfigJson(raw_config_json_)) {
            ESP_LOGI(TAG, "loaded cached config ver=%d", config_.config_version);
        } else {
            ESP_LOGW(TAG, "cached config json invalid; fallback to legacy");
            raw_config_json_.clear();
            MigrateLegacyNvs();
        }
    }
}

void ConfigManager::MigrateLegacyNvs() {
    // 从旧命名空间 deep_dog_mqtt 填补缺失字段
    ApplyLegacyFallback(&config_);

    // 序列化当前 config_ → raw_config_json_（字段名对齐后端模板）
    cJSON* root = cJSON_CreateObject();
    if (root) {
        cJSON* conn = cJSON_AddObjectToObject(root, "connectivity");
        cJSON* mqtt = cJSON_AddObjectToObject(conn, "mqtt");
        cJSON_AddStringToObject(mqtt, "broker_host", config_.mqtt.broker_host.c_str());
        cJSON_AddNumberToObject(mqtt, "broker_port", config_.mqtt.broker_port);
        cJSON_AddBoolToObject(mqtt, "use_tls", config_.mqtt.use_tls);
        cJSON_AddStringToObject(mqtt, "username", config_.mqtt.username.c_str());
        cJSON_AddStringToObject(mqtt, "password", config_.mqtt.password.c_str());
        cJSON_AddStringToObject(mqtt, "topic_root", config_.mqtt.topic_root.c_str());
        cJSON_AddNumberToObject(mqtt, "keepalive_s", config_.mqtt.keepalive_s);
        cJSON* mtx = cJSON_AddObjectToObject(conn, "mediamtx");
        cJSON_AddStringToObject(mtx, "rtsp_host", config_.mediamtx.rtsp_host.c_str());
        cJSON_AddNumberToObject(mtx, "rtsp_port", config_.mediamtx.rtsp_port);
        cJSON_AddStringToObject(mtx, "stream_path_template",
                                config_.mediamtx.stream_path_template.c_str());
        cJSON* api = cJSON_AddObjectToObject(conn, "api_server");
        cJSON_AddStringToObject(api, "base_url", config_.api_server.base_url.c_str());
        cJSON* cc = cJSON_AddObjectToObject(root, "config_center");
        cJSON_AddNumberToObject(cc, "pull_interval_s", config_.config_center.pull_interval_s);
        cJSON_AddBoolToObject(cc, "enable_mqtt_notify", config_.config_center.enable_mqtt_notify);
        char* printed = cJSON_PrintUnformatted(root);
        if (printed) {
            raw_config_json_ = printed;
            cJSON_free(printed);
        }
        cJSON_Delete(root);
    }
    ESP_LOGI(TAG, "legacy fallback applied (broker=%s:%d)", config_.mqtt.broker_host.c_str(),
             config_.mqtt.broker_port);
}

void ConfigManager::ApplyLegacyFallback(DeviceConfig* cfg) {
    const DeepDogMqttSettings s = DeepDogMqttConfig::Load();
    if (cfg->mqtt.broker_host.empty()) {
        cfg->mqtt.broker_host = s.broker_host;
    }
    if (cfg->mqtt.broker_port <= 0) {
        cfg->mqtt.broker_port = s.broker_port;
    }
    if (cfg->mqtt.username.empty()) {
        cfg->mqtt.username = s.username;
    }
    if (cfg->mqtt.password.empty()) {
        cfg->mqtt.password = s.password;
    }
    if (cfg->mqtt.topic_root.empty()) {
        cfg->mqtt.topic_root = s.topic_root;
    }
    cfg->mqtt.use_tls = s.use_tls;
    cfg->mqtt.keepalive_s = s.keepalive_s;
    if (cfg->api_server.base_url.empty()) {
        cfg->api_server.base_url = s.server_api_base;
    }
    if (api_base_url_.empty()) {
        api_base_url_ = s.server_api_base;
    }
}

// ──────────────────────────────────────────────────────────────
//  解析 / 存储
// ──────────────────────────────────────────────────────────────

bool ConfigManager::ParseConfigJson(const std::string& config_json) {
    cJSON* root = cJSON_Parse(config_json.c_str());
    if (!root) {
        return false;
    }

    DeviceConfig cfg = config_;
    cfg.schema_version = JsonStr(root, "schema_version", cfg.schema_version);

    // connectivity.mqtt
    const cJSON* mqtt = JsonGetByPath(root, "connectivity.mqtt");
    if (cJSON_IsObject(mqtt)) {
        cfg.mqtt.broker_host = JsonStr(mqtt, "broker_host", cfg.mqtt.broker_host);
        cfg.mqtt.broker_port = JsonInt(mqtt, "broker_port", cfg.mqtt.broker_port);
        cfg.mqtt.use_tls = JsonBool(mqtt, "use_tls", cfg.mqtt.use_tls);
        cfg.mqtt.username = JsonStr(mqtt, "username", cfg.mqtt.username);
        cfg.mqtt.password = JsonStr(mqtt, "password", cfg.mqtt.password);
        cfg.mqtt.topic_root = JsonStr(mqtt, "topic_root", cfg.mqtt.topic_root);
        cfg.mqtt.keepalive_s = JsonInt(mqtt, "keepalive_s", cfg.mqtt.keepalive_s);
        cfg.mqtt.qos_default = JsonInt(mqtt, "qos_default", cfg.mqtt.qos_default);
    }

    // connectivity.mediamtx
    const cJSON* mtx = JsonGetByPath(root, "connectivity.mediamtx");
    if (cJSON_IsObject(mtx)) {
        cfg.mediamtx.rtsp_host = JsonStr(mtx, "rtsp_host", cfg.mediamtx.rtsp_host);
        cfg.mediamtx.rtsp_port = JsonInt(mtx, "rtsp_port", cfg.mediamtx.rtsp_port);
        cfg.mediamtx.hls_host = JsonStr(mtx, "hls_host", cfg.mediamtx.hls_host);
        cfg.mediamtx.hls_port = JsonInt(mtx, "hls_port", cfg.mediamtx.hls_port);
        cfg.mediamtx.stream_path_template =
            JsonStr(mtx, "stream_path_template", cfg.mediamtx.stream_path_template);
        cfg.mediamtx.push_protocol = JsonStr(mtx, "push_protocol", cfg.mediamtx.push_protocol);
    }

    // connectivity.api_server
    const cJSON* api = JsonGetByPath(root, "connectivity.api_server");
    if (cJSON_IsObject(api)) {
        cfg.api_server.base_url = JsonStr(api, "base_url", cfg.api_server.base_url);
        cfg.api_server.timeout_ms = JsonInt(api, "timeout_ms", cfg.api_server.timeout_ms);
        cfg.api_server.tls_verify = JsonBool(api, "tls_verify", cfg.api_server.tls_verify);
    }

    // config_center（设备端本地策略，后端模板可选提供）
    const cJSON* cc = JsonGetByPath(root, "config_center");
    if (cJSON_IsObject(cc)) {
        cfg.config_center.pull_interval_s =
            JsonInt(cc, "pull_interval_s", cfg.config_center.pull_interval_s);
        cfg.config_center.enable_mqtt_notify =
            JsonBool(cc, "enable_mqtt_notify", cfg.config_center.enable_mqtt_notify);
    }

    // io.ext_pins（CFG-02：38/48 运行时模式）
    const cJSON* ext_pins = JsonGetByPath(root, "io.ext_pins");
    if (cJSON_IsObject(ext_pins)) {
        cfg.ext_pins.mode = JsonStr(ext_pins, "mode", cfg.ext_pins.mode);
        // gpio_a / gpio_b 只读：出现即忽略并在 ack 上报 E_RO_PIN
        if (cJSON_GetObjectItemCaseSensitive(ext_pins, "gpio_a") != nullptr ||
            cJSON_GetObjectItemCaseSensitive(ext_pins, "gpio_b") != nullptr) {
            last_apply_errors_.push_back({"io.ext_pins.gpio_a", "E_RO_PIN",
                                          "gpio_a/gpio_b is read-only; ignored"});
            ESP_LOGW(TAG, "io.ext_pins gpio_a/gpio_b is read-only; ignored");
        }
    }

    // 应用旧 NVS 补齐仍为空的字段
    ApplyLegacyFallback(&cfg);

    std::lock_guard<std::mutex> lock(mutex_);
    config_ = cfg;
    cJSON_Delete(root);
    return true;
}

void ConfigManager::SaveToNvs() {
    Settings st(kNvsNamespace, true);
    st.SetString("config_json", raw_config_json_);
    st.SetInt("config_version", config_.config_version);
    st.SetString("fingerprint", fingerprint_);
    if (!device_secret_.empty()) {
        st.SetString("device_secret", device_secret_);
    }
    if (!api_base_url_.empty()) {
        st.SetString("api_base_url", api_base_url_);
    }
    st.SetInt("last_pull_at", static_cast<int32_t>(last_pull_at_));
}

// ──────────────────────────────────────────────────────────────
//  差异分发
// ──────────────────────────────────────────────────────────────

void ConfigManager::DiffAndNotify(const DeviceConfig& old_cfg, const DeviceConfig& new_cfg) {
    auto& hub = ConfigChangeHub::GetInstance();

    if (old_cfg.mqtt.broker_host != new_cfg.mqtt.broker_host ||
        old_cfg.mqtt.broker_port != new_cfg.mqtt.broker_port ||
        old_cfg.mqtt.username != new_cfg.mqtt.username ||
        old_cfg.mqtt.password != new_cfg.mqtt.password ||
        old_cfg.mqtt.use_tls != new_cfg.mqtt.use_tls ||
        old_cfg.mqtt.topic_root != new_cfg.mqtt.topic_root) {
        hub.Notify("connectivity.mqtt");
    }
    if (old_cfg.mediamtx.rtsp_host != new_cfg.mediamtx.rtsp_host ||
        old_cfg.mediamtx.rtsp_port != new_cfg.mediamtx.rtsp_port ||
        old_cfg.mediamtx.stream_path_template != new_cfg.mediamtx.stream_path_template ||
        old_cfg.mediamtx.push_protocol != new_cfg.mediamtx.push_protocol) {
        hub.Notify("connectivity.mediamtx");
    }
    if (old_cfg.api_server.base_url != new_cfg.api_server.base_url) {
        hub.Notify("connectivity.api_server");
    }
    if (old_cfg.config_center.enable_mqtt_notify != new_cfg.config_center.enable_mqtt_notify ||
        old_cfg.config_center.pull_interval_s != new_cfg.config_center.pull_interval_s) {
        hub.Notify("config_center");
    }
    // CFG-02：38/48 模式变化 → 写 pending（重启生效）
    if (old_cfg.ext_pins.mode != new_cfg.ext_pins.mode) {
        ApplyExtPinMode(new_cfg.ext_pins.mode);
        hub.Notify("io.ext_pins");
    }
}

void ConfigManager::ApplyExtPinMode(const std::string& mode_name) {
    if (mode_name.empty()) {
        return;  // 本次未下发该字段
    }
    deep_dog_ext_pin_mode_t mode = deep_dog_ext_pin::kNoPending;
    if (!deep_dog_ext_pin::StrToMode(mode_name.c_str(), &mode) || !deep_dog_ext_pin::IsSupported(mode)) {
        ESP_LOGW(TAG, "unsupported ext_pins mode '%s'", mode_name.c_str());
        last_apply_errors_.push_back({"io.ext_pins.mode", "E_UNSUPPORTED_MODE",
                                      "unsupported ext_pins mode '" + mode_name + "'"});
        return;
    }
    if (deep_dog_ext_pin::SetPendingMode(mode) != ESP_OK) {
        last_apply_errors_.push_back(
            {"io.ext_pins.mode", "E_APPLY_FAILED", "failed to persist pending ext_pins mode"});
        return;
    }
    ESP_LOGI(TAG, "ext_pins pending=%s (apply after restart)", mode_name.c_str());
}

// ──────────────────────────────────────────────────────────────
//  路径读取
// ──────────────────────────────────────────────────────────────

std::string ConfigManager::GetString(const std::string& path, const std::string& fallback) const {
    std::lock_guard<std::mutex> lock(mutex_);
    cJSON* root = cJSON_Parse(raw_config_json_.c_str());
    if (!root) {
        return fallback;
    }
    const cJSON* item = JsonGetByPath(root, path);
    std::string out = fallback;
    if (cJSON_IsString(item) && item->valuestring) {
        out = item->valuestring;
    }
    cJSON_Delete(root);
    return out;
}

int ConfigManager::GetInt(const std::string& path, int fallback) const {
    std::lock_guard<std::mutex> lock(mutex_);
    cJSON* root = cJSON_Parse(raw_config_json_.c_str());
    if (!root) {
        return fallback;
    }
    const cJSON* item = JsonGetByPath(root, path);
    int out = fallback;
    if (cJSON_IsNumber(item)) {
        out = item->valueint;
    }
    cJSON_Delete(root);
    return out;
}

bool ConfigManager::GetBool(const std::string& path, bool fallback) const {
    std::lock_guard<std::mutex> lock(mutex_);
    cJSON* root = cJSON_Parse(raw_config_json_.c_str());
    if (!root) {
        return fallback;
    }
    const cJSON* item = JsonGetByPath(root, path);
    bool out = fallback;
    if (cJSON_IsBool(item)) {
        out = cJSON_IsTrue(item);
    }
    cJSON_Delete(root);
    return out;
}

/** 解析后的流路径（stream_path_template 代入 device_id） */
std::string ConfigManager::ResolvedStreamPath() const {
    return ResolveTemplate(config_.mediamtx.stream_path_template, device_id_);
}

// ──────────────────────────────────────────────────────────────
//  身份
// ──────────────────────────────────────────────────────────────

void ConfigManager::SetApiBaseUrl(const std::string& url) {
    std::string v = url;
    TrimTrailingSlash(&v);
    api_base_url_ = v;
    std::lock_guard<std::mutex> lock(mutex_);
    config_.api_server.base_url = v;
}

void ConfigManager::SetDeviceSecret(const std::string& secret) {
    device_secret_ = secret;
    SaveToNvs();
}

bool ConfigManager::EnsureRegistered() {
    if (!device_secret_.empty()) {
        return true;
    }
    if (api_base_url_.empty()) {
        ESP_LOGW(TAG, "no api_base_url; skip register");
        return false;
    }
    ConfigApiClient client(ConfigApiClient::Endpoint{api_base_url_, device_id_, ""});
    std::string secret;
    int version = 0;
    if (!client.Register("deep_dog_esp32", device_id_, FirmwareVersion(), device_id_, &secret,
                         &version)) {
        ESP_LOGW(TAG, "register failed");
        return false;
    }
    if (secret.empty()) {
        ESP_LOGW(TAG, "register returned empty secret");
        return false;
    }
    device_secret_ = secret;
    SaveToNvs();
    ESP_LOGI(TAG, "registered; secret acquired");
    return true;
}

void ConfigManager::AckApplied(const std::string& status,
                               const std::vector<ConfigApplyError>& errors) {
    ConfigApiClient* client = MakeClient();
    if (!client) {
        return;
    }
    client->Ack(config_.config_version, status, errors);
    delete client;
}

// ──────────────────────────────────────────────────────────────
//  同步任务
// ──────────────────────────────────────────────────────────────

void ConfigManager::Start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        ESP_LOGW(TAG, "config manager already running");
        return;
    }
    cancel_.store(false, std::memory_order_release);
    if (xTaskCreate(&ConfigManager::TaskEntry, "cfg_sync", kTaskStack, this, kPollTaskPriority,
                    nullptr) != pdPASS) {
        running_.store(false, std::memory_order_release);
        ESP_LOGE(TAG, "failed to create config sync task");
    }
}

void ConfigManager::Stop() {
    cancel_.store(true, std::memory_order_release);
    for (int i = 0; i < 50 && running_.load(std::memory_order_acquire); ++i) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void ConfigManager::RequestSync(const char* reason) {
    sync_reason_ = reason ? reason : "manual";
    sync_requested_.store(true, std::memory_order_release);
}

ConfigApiClient* ConfigManager::MakeClient() const {
    if (api_base_url_.empty()) {
        return nullptr;
    }
    ConfigApiClient::Endpoint ep;
    ep.base_url = api_base_url_;
    ep.device_id = device_id_;
    ep.device_secret = device_secret_;
    return new ConfigApiClient(std::move(ep));
}

void ConfigManager::TaskEntry(void* arg) {
    auto* self = static_cast<ConfigManager*>(arg);
    if (self) {
        self->Run();
        self->running_.store(false, std::memory_order_release);
    }
    vTaskDelete(nullptr);
}

void ConfigManager::Run() {
    // 首次：确保注册 + 立即拉取
    EnsureRegistered();

    int backoff_s = 0;
    while (!cancel_.load(std::memory_order_acquire)) {
        // 决定本轮是否拉取
        const bool triggered = sync_requested_.exchange(false, std::memory_order_acq_rel);
        const bool first = (state_.load(std::memory_order_acquire) == SyncState::kUninitialized);
        if (triggered || first) {
            state_.store(SyncState::kSyncing, std::memory_order_release);
            ConfigApiClient* client = MakeClient();
            if (!client) {
                state_.store(SyncState::kError, std::memory_order_release);
            } else {
                DeviceConfig old_cfg;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    old_cfg = config_;
                }
                PullResponse resp = client->Pull(config_.config_version, fingerprint_,
                                                 {"device.config.v1", config_.schema_version});
                delete client;

                if (resp.result == PullResult::kUpdated) {
                    // 提取 config 子树
                    cJSON* root = cJSON_Parse(resp.raw_json.c_str());
                    if (root) {
                        const cJSON* cfg_item = cJSON_GetObjectItemCaseSensitive(root, "config");
                        char* cfg_str = cfg_item ? cJSON_PrintUnformatted(const_cast<cJSON*>(cfg_item))
                                                 : nullptr;
                        const int new_ver = JsonInt(root, "config_version", config_.config_version);
                        const std::string new_fp = JsonStr(root, "fingerprint", fingerprint_);
                        if (cfg_str) {
                            const std::string cfg_json(cfg_str);
                            cJSON_free(cfg_str);
                            {
                                std::lock_guard<std::mutex> lock(mutex_);
                                raw_config_json_ = cfg_json;
                                config_.config_version = new_ver;
                            }
                            fingerprint_ = new_fp;
                            last_apply_errors_.clear();
                            ParseConfigJson(cfg_json);
                            DiffAndNotify(old_cfg, config_);
                            last_pull_at_ = static_cast<int64_t>(esp_timer_get_time() / 1000000);
                            SaveToNvs();
                            has_remote_.store(true, std::memory_order_release);
                            state_.store(SyncState::kSynced, std::memory_order_release);
                            ESP_LOGI(TAG, "config updated ver=%d fp=%.8s (reason=%s) errors=%u", new_ver,
                                     new_fp.c_str(), sync_reason_,
                                     static_cast<unsigned>(last_apply_errors_.size()));
                            AckApplied(last_apply_errors_.empty() ? "success" : "partial",
                                       last_apply_errors_);
                            last_apply_errors_.clear();
                            backoff_s = 0;
                        } else {
                            state_.store(SyncState::kError, std::memory_order_release);
                        }
                        cJSON_Delete(root);
                    } else {
                        state_.store(SyncState::kError, std::memory_order_release);
                    }
                } else if (resp.result == PullResult::kNotModified) {
                    state_.store(SyncState::kSynced, std::memory_order_release);
                    last_pull_at_ = static_cast<int64_t>(esp_timer_get_time() / 1000000);
                    backoff_s = 0;
                    ESP_LOGD(TAG, "config not modified ver=%d", config_.config_version);
                } else {
                    // 网络/服务器错误 → 退避
                    state_.store(SyncState::kDegraded, std::memory_order_release);
                    backoff_s = (backoff_s == 0)
                                    ? kRetryMinS
                                    : (backoff_s * 2 > kRetryMaxS ? kRetryMaxS : backoff_s * 2);
                    ESP_LOGW(TAG, "pull failed; backoff %ds", backoff_s);
                }
            }
        }

        // 睡眠：按配置间隔轮询；期间被 RequestSync 唤醒则立即拉取
        int interval_s = config_.config_center.pull_interval_s;
        if (interval_s <= 0) {
            interval_s = 300;
        }
        if (backoff_s > 0) {
            interval_s = backoff_s;  // 失败退避优先
        }
        const int slice_ms = 500;
        for (int elapsed_ms = 0; elapsed_ms < interval_s * 1000; elapsed_ms += slice_ms) {
            if (cancel_.load(std::memory_order_acquire)) {
                return;
            }
            if (sync_requested_.load(std::memory_order_acquire)) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(slice_ms));
        }
    }
}

}  // namespace config_center
