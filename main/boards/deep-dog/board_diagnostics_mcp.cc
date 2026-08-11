#include "board_diagnostics_mcp.h"

#include "mqtt/device_diagnostics.h"

#include "mcp_server.h"

#include <cJSON.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>

#define TAG "dog_board_mcp"

void RegisterBoardDiagnosticsMcpTools(McpServer& mcp_server) {
    mcp_server.AddTool(
        "self.board.diagnostics",
        "Board memory and FreeRTOS task stack high-water marks (internal/psram).",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            cJSON* root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "uptime_s", static_cast<double>(esp_timer_get_time() / 1000000LL));
            cJSON_AddNumberToObject(root, "free_heap", static_cast<double>(esp_get_free_heap_size()));
            cJSON_AddNumberToObject(root, "min_free_heap", static_cast<double>(esp_get_minimum_free_heap_size()));

            auto add_mem = [](cJSON* parent, const char* key, uint32_t caps) {
                cJSON* o = cJSON_AddObjectToObject(parent, key);
                cJSON_AddNumberToObject(o, "free", static_cast<double>(heap_caps_get_free_size(caps)));
                cJSON_AddNumberToObject(o, "min", static_cast<double>(heap_caps_get_minimum_free_size(caps)));
                cJSON_AddNumberToObject(o, "total", static_cast<double>(heap_caps_get_total_size(caps)));
            };
            cJSON* mem = cJSON_AddObjectToObject(root, "mem");
            add_mem(mem, "internal", MALLOC_CAP_INTERNAL);
            add_mem(mem, "psram", MALLOC_CAP_SPIRAM);

            DeepDogDeviceDiagnosticsAppendTasks(root);

            char* printed = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);
            if (!printed) {
                return std::string("{}");
            }
            std::string out(printed);
            cJSON_free(printed);
            return out;
        });
    ESP_LOGI(TAG, "registered self.board.diagnostics");
}
