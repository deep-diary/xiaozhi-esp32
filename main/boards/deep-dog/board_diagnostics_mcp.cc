#include "board_diagnostics_mcp.h"

#include "mqtt/memory_report.h"
#include "ws-mcp/ws_mcp_config.h"

#include "mcp_server.h"

#include <wifi_manager.h>

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <cstdio>
#include <string>

#define TAG "dog_board_mcp"

void RegisterBoardDiagnosticsMcpTools(McpServer& mcp_server) {
    mcp_server.AddTool(
        "self.board.get_ip",
        "获取本机 WiFi IP 与局域网 WebSocket MCP 地址。"
        "用户问「IP 是多少」「局域网地址」「WebSocket 怎么连」「WS 地址」时调用，"
        "并把 ip / ws_url 念给用户（便于在浏览器手输）。",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            const std::string ip = WifiManager::GetInstance().GetIpAddress();
            cJSON* root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "ip", ip.c_str());
            cJSON_AddBoolToObject(root, "connected", !ip.empty());
#if DEEP_DOG_WS_MCP_ENABLE
            if (!ip.empty()) {
                char ws_url[96];
                snprintf(ws_url, sizeof(ws_url), "ws://%s:%u%s", ip.c_str(),
                         static_cast<unsigned>(DEEP_DOG_WS_MCP_PORT), DEEP_DOG_WS_MCP_PATH);
                cJSON_AddStringToObject(root, "ws_url", ws_url);
                cJSON_AddNumberToObject(root, "ws_mcp_port", DEEP_DOG_WS_MCP_PORT);
                cJSON_AddStringToObject(root, "ws_mcp_path", DEEP_DOG_WS_MCP_PATH);
            } else {
                cJSON_AddStringToObject(root, "ws_url", "");
            }
#else
            cJSON_AddStringToObject(root, "ws_url", "");
#endif
            char* printed = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
            if (!printed) {
                return std::string("{\"ip\":\"\",\"connected\":false,\"ws_url\":\"\"}");
            }
            std::string out(printed);
            cJSON_free(printed);
            return out;
        });

    mcp_server.AddTool(
        "self.board.diagnostics",
        "Board memory and FreeRTOS task stack high-water marks (internal/psram).",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            cJSON* root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "uptime_s", static_cast<double>(esp_timer_get_time() / 1000000LL));
            cJSON_AddNumberToObject(root, "free_heap", static_cast<double>(esp_get_free_heap_size()));
            cJSON_AddNumberToObject(root, "min_free_heap", static_cast<double>(esp_get_minimum_free_heap_size()));

            const std::string ip = WifiManager::GetInstance().GetIpAddress();
            cJSON_AddStringToObject(root, "ip", ip.c_str());

            DeepDogMemoryReportAppend(root);

            char* printed = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
            if (!printed) {
                return std::string("{}");
            }
            std::string out(printed);
            cJSON_free(printed);
            return out;
        });
    ESP_LOGI(TAG, "registered self.board.get_ip + self.board.diagnostics");
}
