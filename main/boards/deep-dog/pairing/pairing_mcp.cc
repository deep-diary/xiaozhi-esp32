#include "pairing/pairing_mcp.h"

#include "board_features.h"
#include "mqtt/deep_dog_mqtt.h"
#include "mcp_server.h"

#include <cJSON.h>
#include <esp_log.h>

#include <string>

#define TAG "dog_pair_mcp"

void RegisterPairingMcpTools(McpServer& mcp_server, DeepDogMqtt* mqtt) {
#if !DEEP_DOG_MQTT_ENABLE
    (void)mcp_server;
    (void)mqtt;
    return;
#else
    if (!mqtt) {
        return;
    }

    mcp_server.AddTool(
        "self.device.start_pairing",
        "添加设备、配对、绑定设备。用户说「添加设备」「配对」「绑定到账号」时调用。"
        "未绑定时生成 6 位配对码并语音播报；已绑定时提示已绑定。",
        PropertyList(),
        [mqtt](const PropertyList&) -> ReturnValue {
            if (!mqtt->IsRunning()) {
                return std::string("MQTT 未连接，请稍后重试");
            }
            mqtt->StartPairingSessionOrAnnounceBound();
            cJSON* root = cJSON_CreateObject();
            cJSON_AddBoolToObject(root, "bound", mqtt->IsDeviceBound());
            if (!mqtt->IsDeviceBound()) {
                cJSON_AddStringToObject(root, "pair_code", mqtt->DevicePairCode().c_str());
                cJSON_AddStringToObject(root, "hint", "请在网页输入配对码完成绑定");
            } else {
                cJSON_AddStringToObject(root, "message", "本设备已绑定");
            }
            char* raw = cJSON_PrintUnformatted(root);
            std::string out = raw ? raw : "{}";
            if (raw) {
                cJSON_free(raw);
            }
            cJSON_Delete(root);
            return out;
        });

    mcp_server.AddTool(
        "self.device.unbind",
        "解绑、删除设备、从账号移除。用户说「解绑」「删除设备」时调用。"
        "已绑定时向服务器发送解绑请求；未绑定时提示未绑定。",
        PropertyList(),
        [mqtt](const PropertyList&) -> ReturnValue {
            if (!mqtt->IsRunning()) {
                return std::string("MQTT 未连接，请稍后重试");
            }
            if (!mqtt->IsDeviceBound()) {
                return std::string("当前未绑定");
            }
            mqtt->RequestDeviceUnbind();
            return std::string("解绑请求已发送，请稍候");
        });

    ESP_LOGI(TAG, "MCP pairing tools registered");
#endif
}
