#include "net/deep_dog_sntp.h"

#include "net/net_config.h"

#include <esp_log.h>
#include <esp_netif_sntp.h>
#include <esp_timer.h>

#include <ctime>

#define TAG "dog_sntp"

namespace {

constexpr time_t kMinPlausibleUnix = 1000000000;

DeepDogClockSyncedFn s_on_synced = nullptr;
bool s_inited = false;
bool s_sync_notified = false;
esp_timer_handle_t s_defer_timer = nullptr;

void RunOnSynced(void* /*arg*/) {
    if (s_on_synced) {
        s_on_synced();
    }
}

void EnsureDeferTimer() {
    if (s_defer_timer) {
        return;
    }
    esp_timer_create_args_t args = {
        .callback = &RunOnSynced,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_sntp_cb",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&args, &s_defer_timer) != ESP_OK) {
        ESP_LOGE(TAG, "defer timer create failed");
    }
}

void OnSntpSync(struct timeval* tv) {
    if (!tv) {
        return;
    }
    ESP_LOGI(TAG, "SNTP synced unix=%lu", static_cast<unsigned long>(tv->tv_sec));
    if (s_sync_notified) {
        return;
    }
    s_sync_notified = true;
    if (!s_on_synced) {
        return;
    }
    EnsureDeferTimer();
    if (!s_defer_timer) {
        return;
    }
    (void)esp_timer_stop(s_defer_timer);
    (void)esp_timer_start_once(s_defer_timer, 1);
}

}  // namespace

void DeepDogSntpInit(void) {
#if !DEEP_DOG_SNTP_ENABLE
    ESP_LOGI(TAG, "SNTP disabled (DEEP_DOG_SNTP_ENABLE=0)");
    return;
#endif
    if (s_inited) {
        return;
    }
    if (DeepDogClockIsSynced()) {
        ESP_LOGI(TAG, "clock already synced (OTA/manual), starting SNTP for drift");
    }

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(DEEP_DOG_SNTP_SERVER_0);
    config.wait_for_sync = false;
    config.sync_cb = &OnSntpSync;

    const esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_sntp_init failed: %s", esp_err_to_name(err));
        return;
    }
    s_inited = true;
    ESP_LOGI(TAG, "SNTP init server=%s (backup=%s if CONFIG_LWIP_SNTP_MAX_SERVERS>1)",
             DEEP_DOG_SNTP_SERVER_0, DEEP_DOG_SNTP_SERVER_1);
}

bool DeepDogClockIsSynced(void) {
    return time(nullptr) >= kMinPlausibleUnix;
}

uint32_t DeepDogNowUnixSec(void) {
    const time_t now = time(nullptr);
    if (now >= kMinPlausibleUnix) {
        return static_cast<uint32_t>(now);
    }
    return static_cast<uint32_t>(esp_timer_get_time() / 1000000LL);
}

void DeepDogSntpSetOnSynced(DeepDogClockSyncedFn fn) {
    s_on_synced = fn;
    if (fn && DeepDogClockIsSynced() && !s_sync_notified) {
        s_sync_notified = true;
        EnsureDeferTimer();
        if (s_defer_timer) {
            (void)esp_timer_stop(s_defer_timer);
            (void)esp_timer_start_once(s_defer_timer, 1);
        }
    }
}
