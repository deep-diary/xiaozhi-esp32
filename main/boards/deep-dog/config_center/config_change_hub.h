#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace config_center {

/** 配置路径变更回调：path 为发生变更的 JSON 路径，如 "connectivity.mqtt" */
using ChangeCallback = std::function<void(const std::string& path)>;

/**
 * 配置变更事件分发中心（观察者模式）。
 *
 * 模块按 JSON 路径前缀订阅（如 "connectivity.mqtt"）；
 * ConfigManager 应用新配置后，按差异路径通知对应订阅者。
 * 线程安全：订阅/退订/通知均可跨任务调用。
 */
class ConfigChangeHub {
public:
    static ConfigChangeHub& GetInstance();

    /** 订阅路径前缀变更；返回订阅 ID（用于退订） */
    int Subscribe(const std::string& path_prefix, ChangeCallback cb);

    /** 退订 */
    void Unsubscribe(int sub_id);

    /** 通知某路径已变更：触发所有匹配（等值或前缀）的订阅者 */
    void Notify(const std::string& path);

    /** 清空全部订阅（测试/重启用） */
    void Clear();

private:
    ConfigChangeHub() = default;

    struct Entry {
        std::string prefix;
        ChangeCallback cb;
    };

    std::mutex mutex_;
    int next_id_ = 1;
    std::unordered_map<int, Entry> subs_;
};

}  // namespace config_center
