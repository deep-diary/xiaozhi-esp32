#include "config_center/config_change_hub.h"

#include <esp_log.h>

#define TAG "cfg_hub"

namespace config_center {

ConfigChangeHub& ConfigChangeHub::GetInstance() {
    static ConfigChangeHub instance;
    return instance;
}

int ConfigChangeHub::Subscribe(const std::string& path_prefix, ChangeCallback cb) {
    if (!cb) {
        return -1;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const int id = next_id_++;
    subs_[id] = Entry{path_prefix, std::move(cb)};
    return id;
}

void ConfigChangeHub::Unsubscribe(int sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    subs_.erase(sub_id);
}

void ConfigChangeHub::Notify(const std::string& path) {
    std::vector<Entry> matched;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& kv : subs_) {
            const std::string& prefix = kv.second.prefix;
            // 等值或前缀匹配（"connectivity" 命中 "connectivity.mqtt.host"）
            if (prefix.empty() || path == prefix || path.rfind(prefix + ".", 0) == 0 ||
                prefix.rfind(path + ".", 0) == 0) {
                matched.push_back(kv.second);
            }
        }
    }
    // 回调在锁外执行，避免回调内再次订阅/通知死锁
    for (auto& e : matched) {
        ESP_LOGD(TAG, "notify path=%s", path.c_str());
        if (e.cb) {
            e.cb(path);
        }
    }
}

void ConfigChangeHub::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    subs_.clear();
    next_id_ = 1;
}

}  // namespace config_center
