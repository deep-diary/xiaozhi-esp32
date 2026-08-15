#include "board_diagnostics_mcp.h"

#include "mqtt/memory_report.h"

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
    ESP_LOGI(TAG, "registered self.board.diagnostics");
}
