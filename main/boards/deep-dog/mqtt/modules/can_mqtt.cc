#include "mqtt/modules/can_mqtt.h"

#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_topics.h"
#include "mqtt/modules/envelope.h"
#include "config.h"
#include "can/can_config.h"
#include "can/can_frame_hub.h"
#include "can/ESP32-TWAI-CAN.hpp"
#include "mqtt/modules/can_datapoints.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#define TAG "dog_mqtt_can"

#if DEEP_DOG_CAN_ENABLE

namespace {

int64_t UnixTs() {
    const time_t now = time(nullptr);
    if (now > 1000000000) {
        return static_cast<int64_t>(now);
    }
    return static_cast<int64_t>(esp_timer_get_time() / 1000000LL);
}

cJSON* FrameToJson(const CanFrame* frame, bool is_tx) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "dir", is_tx ? "tx" : "rx");
    cJSON_AddNumberToObject(o, "id", static_cast<double>(frame->identifier));
    char hex[16];
    snprintf(hex, sizeof(hex), "0x%08lX", static_cast<unsigned long>(frame->identifier));
    cJSON_AddStringToObject(o, "id_hex", hex);
    cJSON_AddBoolToObject(o, "ext", frame->extd);
    cJSON_AddBoolToObject(o, "rtr", frame->rtr);
    const int dlc = frame->data_length_code > 8 ? 8 : frame->data_length_code;
    cJSON_AddNumberToObject(o, "dlc", dlc);
    char data_hex[17] = {0};
    for (int i = 0; i < dlc; ++i) {
        snprintf(data_hex + i * 2, 3, "%02X", frame->data[i]);
    }
    cJSON_AddStringToObject(o, "data_hex", data_hex);
    cJSON_AddNumberToObject(o, "ts_ms", static_cast<double>(esp_timer_get_time() / 1000LL));
    return o;
}

bool ParseHexByte(const char* p, uint8_t* out) {
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    const int hi = nib(p[0]);
    const int lo = nib(p[1]);
    if (hi < 0 || lo < 0) {
        return false;
    }
    *out = static_cast<uint8_t>((hi << 4) | lo);
    return true;
}

}  // namespace

DeepDogCanMqtt::DeepDogCanMqtt(DeepDogMqttClient* client) : client_(client) {
    batch_mutex_ = xSemaphoreCreateMutex();
    esp_timer_handle_t timer = nullptr;
    esp_timer_create_args_t args = {
        .callback = &DeepDogCanMqtt::FlushTimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_can_mqtt",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&args, &timer) == ESP_OK) {
        flush_timer_ = timer;
    }
#if DEEP_DOG_CAN_HEARTBEAT_MS > 0
    esp_timer_handle_t hb_timer = nullptr;
    esp_timer_create_args_t hb_args = {
        .callback = &DeepDogCanMqtt::HeartbeatTimerCb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "dog_can_hb",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&hb_args, &hb_timer) == ESP_OK) {
        heartbeat_timer_ = hb_timer;
    }
#endif
}

DeepDogCanMqtt::~DeepDogCanMqtt() {
    Stop();
    if (flush_timer_) {
        esp_timer_delete(static_cast<esp_timer_handle_t>(flush_timer_));
        flush_timer_ = nullptr;
    }
    if (heartbeat_timer_) {
        esp_timer_delete(static_cast<esp_timer_handle_t>(heartbeat_timer_));
        heartbeat_timer_ = nullptr;
    }
    if (batch_mutex_) {
        vSemaphoreDelete(static_cast<SemaphoreHandle_t>(batch_mutex_));
        batch_mutex_ = nullptr;
    }
    if (pending_frames_) {
        cJSON_Delete(pending_frames_);
        pending_frames_ = nullptr;
    }
}

void DeepDogCanMqtt::FlushTimerCb(void* arg) {
    auto* self = static_cast<DeepDogCanMqtt*>(arg);
    if (self) {
        self->FlushFrames();
    }
}

void DeepDogCanMqtt::HeartbeatTimerCb(void* arg) {
    auto* self = static_cast<DeepDogCanMqtt*>(arg);
    if (self) {
        self->EmitHeartbeat();
    }
}

/**
 * 采样总线链路质量：rx_fps = 本窗口内从总线读到的帧数 / 秒（与是否上报、是否节流无关）。
 * 用于量化「物理层/X 收链路」好坏：正常应接近总线实际帧率（如 20ms 周期帧 → ~50 帧/s）。
 */
void DeepDogCanMqtt::UpdateLinkStats() {
    const int64_t now = esp_timer_get_time();
    if (last_stats_us_ != 0 && (now - last_stats_us_) < 1000000) {
        return;  // ≥1s 才重算，避免状态点/心跳两处重复采样
    }
    DeepDogCanHubStats stats {};
    DeepDogCanGetStats(&stats);
    if (last_stats_us_ != 0) {
        const double dt = static_cast<double>(now - last_stats_us_) / 1000000.0;
        if (dt > 0.2) {
            rx_fps_ = static_cast<double>(stats.rx_frames - last_rx_frames_) / dt;
        }
    }
    last_rx_frames_ = stats.rx_frames;
    last_stats_us_ = now;
}

/**
 * 联调心跳：构造一条「模拟 RX」扩展帧并送入与真实帧相同的管线（不写总线）。
 * 用途：把「CAN→MQTT→网页」链路故障与「总线无报文」区分开。见 12-can.md。
 */
void DeepDogCanMqtt::EmitHeartbeat() {
    UpdateLinkStats();  // 心跳每 2s 一次，顺带刷新 rx_fps（不受 tunnel/连接状态影响）
    if (!enabled_ || !connected_ || !tunnel_) {
        return;
    }
    CanFrame frame {};
    frame.identifier = DEEP_DOG_CAN_HEARTBEAT_ID;
    frame.extd = 1;
    frame.rtr = 0;
    frame.data_length_code = 8;
    const uint32_t seq = ++hb_seq_;
    const uint32_t uptime_s = static_cast<uint32_t>(esp_timer_get_time() / 1000000LL);
    frame.data[0] = static_cast<uint8_t>(seq >> 24);
    frame.data[1] = static_cast<uint8_t>(seq >> 16);
    frame.data[2] = static_cast<uint8_t>(seq >> 8);
    frame.data[3] = static_cast<uint8_t>(seq);
    frame.data[4] = static_cast<uint8_t>(uptime_s >> 24);
    frame.data[5] = static_cast<uint8_t>(uptime_s >> 16);
    frame.data[6] = static_cast<uint8_t>(uptime_s >> 8);
    frame.data[7] = static_cast<uint8_t>(uptime_s);
    DeepDogCanNotifyFrame(&frame, 0);
}

void DeepDogCanMqtt::FrameListener(const CanFrame* frame, int is_tx, void* ctx) {
    auto* self = static_cast<DeepDogCanMqtt*>(ctx);
    if (self) {
        self->OnFrame(frame, is_tx != 0);
    }
}

void DeepDogCanMqtt::OnFrame(const CanFrame* frame, bool is_tx) {
    if (!enabled_ || !connected_ || !tunnel_ || !frame) {
        return;
    }
    if (is_tx && !mirror_tx_) {
        return;
    }
    if (ext_only_ && !frame->extd) {
        return;
    }

    auto* mtx = static_cast<SemaphoreHandle_t>(batch_mutex_);
    if (mtx) {
        xSemaphoreTake(mtx, portMAX_DELAY);
    }
    if (!pending_frames_) {
        pending_frames_ = cJSON_CreateArray();
    }
    const int n = cJSON_GetArraySize(pending_frames_);
    if (n >= batch_max_) {
        ++dropped_;
        if (mtx) {
            xSemaphoreGive(mtx);
        }
        return;
    }
    cJSON_AddItemToArray(pending_frames_, FrameToJson(frame, is_tx));
    if (mtx) {
        xSemaphoreGive(mtx);
    }

    const int64_t now = esp_timer_get_time();
    const int64_t min_interval = max_hz_ > 0 ? (1000000LL / max_hz_) : 20000;
    if (now - last_flush_us_ >= min_interval || n + 1 >= batch_max_) {
        FlushFrames();
    } else if (flush_timer_) {
        auto* timer = static_cast<esp_timer_handle_t>(flush_timer_);
        esp_timer_stop(timer);
        const int64_t wait = min_interval - (now - last_flush_us_);
        esp_timer_start_once(timer, wait > 1000 ? wait : 1000);
    }
}

void DeepDogCanMqtt::FlushFrames() {
    if (!enabled_ || !connected_ || !client_) {
        return;
    }
    cJSON* batch = nullptr;
    uint32_t dropped = 0;
    auto* mtx = static_cast<SemaphoreHandle_t>(batch_mutex_);
    if (mtx) {
        xSemaphoreTake(mtx, portMAX_DELAY);
    }
    batch = pending_frames_;
    pending_frames_ = nullptr;
    dropped = dropped_;
    dropped_ = 0;
    if (mtx) {
        xSemaphoreGive(mtx);
    }
    if (!batch || cJSON_GetArraySize(batch) == 0) {
        if (batch) {
            cJSON_Delete(batch);
        }
        return;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "frames", batch);
    cJSON_AddNumberToObject(root, "dropped", static_cast<double>(dropped));
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));
    const std::string payload =
        deep_dog_envelope::Wrap(root, deep_dog_envelope::SchemaStatus(), client_->settings().device_id);
    if (!payload.empty()) {
        client_->Publish(deep_dog_topics::Status("can/frames"), payload, 0, false);
        last_flush_us_ = esp_timer_get_time();
    }
}

bool DeepDogCanMqtt::PublishStatus(bool force) {
    (void)force;
    if (!enabled_ || !connected_ || !client_) {
        return false;
    }
    UpdateLinkStats();
    DeepDogCanHubStats stats {};
    DeepDogCanGetStats(&stats);
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "tunnel", tunnel_);
    cJSON_AddBoolToObject(root, "allow_tx", allow_tx_);
    cJSON_AddNumberToObject(root, "bitrate_kbps", 1000);
    cJSON_AddNumberToObject(root, "tx_gpio", static_cast<double>(CAN_TX_GPIO));
    cJSON_AddNumberToObject(root, "rx_gpio", static_cast<double>(CAN_RX_GPIO));
    cJSON_AddNumberToObject(root, "bus_state", static_cast<double>(ESP32Can.canState()));
    cJSON_AddNumberToObject(root, "rx_err", static_cast<double>(ESP32Can.rxErrorCounter()));
    cJSON_AddNumberToObject(root, "tx_err", static_cast<double>(ESP32Can.txErrorCounter()));
    cJSON_AddNumberToObject(root, "bus_err", static_cast<double>(ESP32Can.busErrCounter()));
    cJSON_AddNumberToObject(root, "dropped", static_cast<double>(dropped_));
    // 链路质量（与 tunnel/节流无关，用于判断物理层好坏；见 12-can.md）
    cJSON_AddNumberToObject(root, "rx_fps", rx_fps_);
    cJSON_AddNumberToObject(root, "rx_total", static_cast<double>(stats.rx_frames));
    cJSON_AddNumberToObject(root, "tx_total", static_cast<double>(stats.tx_frames));
    cJSON_AddNumberToObject(root, "bus_off_count", static_cast<double>(stats.bus_off_events));
    cJSON_AddNumberToObject(root, "recover_count", static_cast<double>(stats.recover_count));
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(UnixTs()));
    const std::string payload =
        deep_dog_envelope::Wrap(root, deep_dog_envelope::SchemaStatus(), client_->settings().device_id);
    if (payload.empty()) {
        return false;
    }
    return client_->Publish(deep_dog_topics::Status("can"), payload, 0, true);
}

void DeepDogCanMqtt::RegisterDataPoints() {
    CanDataPoints_Register(&tunnel_, &allow_tx_, &dropped_);
}

void DeepDogCanMqtt::ApplyCmd(const char* json) {
    cJSON* root = deep_dog_envelope::ParseBusiness(json);
    if (!root) {
        return;
    }
    const cJSON* j = cJSON_GetObjectItem(root, "tunnel");
    if (cJSON_IsBool(j)) {
        tunnel_ = cJSON_IsTrue(j);
    }
    j = cJSON_GetObjectItem(root, "mirror_tx");
    if (cJSON_IsBool(j)) {
        mirror_tx_ = cJSON_IsTrue(j);
    }
    j = cJSON_GetObjectItem(root, "allow_tx");
    if (cJSON_IsBool(j)) {
        allow_tx_ = cJSON_IsTrue(j);
        if (allow_tx_) {
            ESP_LOGW(TAG, "can allow_tx ENABLED — web may inject frames");
        }
    }
    j = cJSON_GetObjectItem(root, "ext_only");
    if (cJSON_IsBool(j)) {
        ext_only_ = cJSON_IsTrue(j);
    }
    j = cJSON_GetObjectItem(root, "max_hz");
    if (cJSON_IsNumber(j) && j->valueint > 0 && j->valueint <= 200) {
        max_hz_ = j->valueint;
    }
    j = cJSON_GetObjectItem(root, "batch_max");
    if (cJSON_IsNumber(j) && j->valueint > 0 && j->valueint <= 64) {
        batch_max_ = j->valueint;
    }
    cJSON_Delete(root);
    PublishStatus(true);
}

void DeepDogCanMqtt::HandleTx(const char* json) {
    if (!allow_tx_) {
        ESP_LOGW(TAG, "can/tx rejected (allow_tx=false)");
        return;
    }
    cJSON* root = deep_dog_envelope::ParseBusiness(json);
    if (!root) {
        return;
    }
    const cJSON* frames = cJSON_GetObjectItem(root, "frames");
    if (!cJSON_IsArray(frames)) {
        cJSON_Delete(root);
        return;
    }
    const cJSON* item = nullptr;
    cJSON_ArrayForEach(item, frames) {
        CanFrame frame {};
        const cJSON* id_j = cJSON_GetObjectItem(item, "id");
        const cJSON* id_hex = cJSON_GetObjectItem(item, "id_hex");
        if (cJSON_IsNumber(id_j)) {
            frame.identifier = static_cast<uint32_t>(id_j->valuedouble);
        } else if (cJSON_IsString(id_hex) && id_hex->valuestring) {
            frame.identifier = static_cast<uint32_t>(strtoul(id_hex->valuestring, nullptr, 0));
        } else {
            continue;
        }
        const cJSON* ext = cJSON_GetObjectItem(item, "ext");
        frame.extd = cJSON_IsTrue(ext);
        const cJSON* rtr = cJSON_GetObjectItem(item, "rtr");
        frame.rtr = cJSON_IsTrue(rtr);
        const cJSON* dlc_j = cJSON_GetObjectItem(item, "dlc");
        int dlc = cJSON_IsNumber(dlc_j) ? dlc_j->valueint : 0;
        if (dlc < 0) {
            dlc = 0;
        }
        if (dlc > 8) {
            dlc = 8;
        }
        frame.data_length_code = static_cast<uint8_t>(dlc);
        const cJSON* data_hex = cJSON_GetObjectItem(item, "data_hex");
        if (cJSON_IsString(data_hex) && data_hex->valuestring) {
            const char* p = data_hex->valuestring;
            const size_t len = strlen(p);
            for (int i = 0; i < dlc && (size_t)(i * 2 + 1) < len; ++i) {
                ParseHexByte(p + i * 2, &frame.data[i]);
            }
        }
        if (ESP32Can.writeFrame(frame, 20)) {
            DeepDogCanNotifyFrame(&frame, 1);
        }
    }
    cJSON_Delete(root);
}

void DeepDogCanMqtt::OnConnected() {
    if (!enabled_ || !client_) {
        return;
    }
    connected_ = true;
    DeepDogCanSetFrameListener(&DeepDogCanMqtt::FrameListener, this);
    client_->Subscribe(deep_dog_topics::Cmd("can"), 1);
    client_->Subscribe(deep_dog_topics::Cmd("can/tx"), 1);
    PublishStatus(true);
#if DEEP_DOG_CAN_HEARTBEAT_MS > 0
    if (heartbeat_timer_) {
        auto* hb = static_cast<esp_timer_handle_t>(heartbeat_timer_);
        esp_timer_stop(hb);
        esp_timer_start_periodic(hb, static_cast<uint64_t>(DEEP_DOG_CAN_HEARTBEAT_MS) * 1000ULL);
        ESP_LOGI(TAG, "can heartbeat on: id=0x%08lX period=%d ms", (unsigned long)DEEP_DOG_CAN_HEARTBEAT_ID,
                 (int)DEEP_DOG_CAN_HEARTBEAT_MS);
    }
#endif
}

void DeepDogCanMqtt::OnDisconnected() {
    connected_ = false;
    DeepDogCanSetFrameListener(nullptr, nullptr);
#if DEEP_DOG_CAN_HEARTBEAT_MS > 0
    if (heartbeat_timer_) {
        esp_timer_stop(static_cast<esp_timer_handle_t>(heartbeat_timer_));
    }
#endif
}

void DeepDogCanMqtt::Stop() {
    OnDisconnected();
    if (flush_timer_) {
        esp_timer_stop(static_cast<esp_timer_handle_t>(flush_timer_));
    }
}

void DeepDogCanMqtt::OnMessage(const std::string& topic, const std::string& payload) {
    if (!enabled_ || !client_) {
        return;
    }
    if (topic == client_->Topic(deep_dog_topics::Cmd("can"))) {
        ApplyCmd(payload.c_str());
        return;
    }
    if (topic == client_->Topic(deep_dog_topics::Cmd("can/tx"))) {
        HandleTx(payload.c_str());
    }
}

#else  // !DEEP_DOG_CAN_ENABLE

DeepDogCanMqtt::DeepDogCanMqtt(DeepDogMqttClient* client) : client_(client) {}
DeepDogCanMqtt::~DeepDogCanMqtt() {}
void DeepDogCanMqtt::OnConnected() {}
void DeepDogCanMqtt::OnDisconnected() {}
void DeepDogCanMqtt::Stop() {}
void DeepDogCanMqtt::OnMessage(const std::string&, const std::string&) {}
bool DeepDogCanMqtt::PublishStatus(bool) { return false; }
void DeepDogCanMqtt::RegisterDataPoints() {}
void DeepDogCanMqtt::FlushTimerCb(void*) {}
void DeepDogCanMqtt::HeartbeatTimerCb(void*) {}
void DeepDogCanMqtt::FrameListener(const CanFrame*, int, void*) {}
void DeepDogCanMqtt::OnFrame(const CanFrame*, bool) {}
void DeepDogCanMqtt::FlushFrames() {}
void DeepDogCanMqtt::EmitHeartbeat() {}
void DeepDogCanMqtt::ApplyCmd(const char*) {}
void DeepDogCanMqtt::HandleTx(const char*) {}

#endif
