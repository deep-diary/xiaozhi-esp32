#pragma once

#include <atomic>
#include <functional>
#include <string>

/**
 * deep-trace HTTP 配对（见 docs/requirements/mqtt/modules/16-http-pairing.md）。
 * 握手领码 → 屏显 + 语音 → 轮询确认 → 应用身份（回调触发主循环 reload）。
 * 独立任务，不阻塞主循环；支持 Cancel。
 */
class DeepDogPairHttp {
public:
    DeepDogPairHttp();
    ~DeepDogPairHttp();

    /** 绑定成功后触发主循环重载 MQTT 身份/推流 path */
    void SetIdentityReloadCallback(std::function<void()> cb);

    /** 未绑定：启动握手 + 轮询；非阻塞（内部起任务） */
    void Start();
    void Cancel();
    bool IsRunning() const { return running_.load(std::memory_order_acquire); }

private:
    static void TaskEntry(void* arg);
    void Run();
    void AnnounceCode(const std::string& code);
    bool ApplyBoundJson(const char* json);

    std::function<void()> identity_reload_cb_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> running_{false};
};
