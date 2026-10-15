#pragma once

#include "config_center/config_types.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace config_center {

class ConfigApiClient;

/** 同步状态 */
enum class SyncState {
    kUninitialized,  ///< 未初始化
    kSyncing,        ///< 同步中
    kSynced,         ///< 已从服务器同步
    kDegraded,       ///< 服务器不可达，使用 NVS 缓存
    kError,          ///< 同步失败（已重试）
};

/**
 * 统一配置中心管理器（单例）。
 *
 * 职责：
 *   - 启动时从 NVS 加载缓存配置（含旧命名空间迁移）
 *   - 从 deep-trace 拉取最新配置并解析
 *   - 对比新旧配置 → 通过 ConfigChangeHub 分发变更
 *   - 后台任务：首次拉取 + 定时轮询 + MQTT 通知触发
 *   - 上报 ack
 *
 * 线程安全：config() 等读取在配置切换时短暂持锁。
 */
class ConfigManager {
public:
    static ConfigManager& GetInstance();

    /** 初始化：加载 NVS 缓存 + 旧命名空间迁移 + 确保 device_secret/api_base */
    void Init();

    /** 启动后台同步任务（首次 pull + 定时轮询） */
    void Start();

    /** 停止后台任务 */
    void Stop();

    /** 触发一次立即拉取（MQTT 通知 / 手动），异步 */
    void RequestSync(const char* reason);

    /** 当前生效配置（只读快照引用；调用方不应长期持有） */
    const DeviceConfig& config() const { return config_; }

    bool has_remote_config() const { return has_remote_; }
    SyncState state() const { return state_.load(std::memory_order_acquire); }
    int config_version() const { return config_.config_version; }
    std::string fingerprint() const { return fingerprint_; }

    /** 设备身份 */
    const std::string& device_id() const { return device_id_; }
    const std::string& device_secret() const { return device_secret_; }
    const std::string& api_base_url() const { return api_base_url_; }

    /** 更新 API 基址（配网页 / 配对下发） */
    void SetApiBaseUrl(const std::string& url);

    /** 更新设备密钥（注册 / 配对下发） */
    void SetDeviceSecret(const std::string& secret);

    /** 注册设备（若无密钥）；返回是否成功 */
    bool EnsureRegistered();

    /** 上报生效结果（同步阻塞，供后台任务/主循环调用）；errors 为 `{field, code, message}` */
    void AckApplied(const std::string& status, const std::vector<ConfigApplyError>& errors);

    /** 点分路径读取（如 "connectivity.mqtt.broker_host"）；无则 fallback */
    std::string GetString(const std::string& path, const std::string& fallback = "") const;
    int GetInt(const std::string& path, int fallback = 0) const;
    bool GetBool(const std::string& path, bool fallback = false) const;

    /** 解析后的流路径（stream_path_template 代入 device_id），如 "deep-dog/aabbccddeeff" */
    std::string ResolvedStreamPath() const;

    /** 原始 config JSON（调试/透传用） */
    const std::string& raw_config_json() const { return raw_config_json_; }

private:
    ConfigManager() = default;
    ~ConfigManager();
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    static void TaskEntry(void* arg);
    void Run();

    void LoadFromNvs();
    void MigrateLegacyNvs();
    void SaveToNvs();
    /** 解析 JSON（config 子树）→ 填充 config_ 结构；返回是否有变更 */
    bool ParseConfigJson(const std::string& config_json);
    /** 对比 old/new，按差异路径 Notify */
    void DiffAndNotify(const DeviceConfig& old_cfg, const DeviceConfig& new_cfg);
    /** CFG-02：下发 io.ext_pins.mode → 校验 + 写 NVS pending（重启生效）；错误进 last_apply_errors_ */
    void ApplyExtPinMode(const std::string& mode_name);
    /** 用旧 NVS（deep_dog_mqtt）填补缺失字段 */
    void ApplyLegacyFallback(DeviceConfig* cfg);

    ConfigApiClient* MakeClient() const;

    mutable std::mutex mutex_;
    DeviceConfig config_;
    std::string raw_config_json_;
    std::string fingerprint_;

    std::string device_id_;
    std::string device_secret_;
    std::string api_base_url_;

    std::atomic<bool> has_remote_{false};
    std::atomic<SyncState> state_{SyncState::kUninitialized};
    std::atomic<bool> sync_requested_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> cancel_{false};
    int64_t last_pull_at_ = 0;
    const char* sync_reason_ = "init";
    /** 本轮配置应用的错误列表（ack `errors[]` 内容，元素为 `{field, code, message}`） */
    std::vector<ConfigApplyError> last_apply_errors_;
};

}  // namespace config_center
