#include "face_mcp.h"

#include "face_control.h"
#include "face_ai_config.h"
#include "face_ai_types.h"
#include "mcp_server.h"

#include <cJSON.h>
#include <esp_log.h>

#include <cstring>
#include <string>
#include <vector>

#define TAG "dog_face_mcp"

#if DEEP_DOG_FACE_AI_ENABLE

namespace {

std::string EnrolledListJson(bool include_live) {
    std::vector<DeepDogFaceEnrolledEntry> entries;
    (void)DeepDogFaceControlListEnrolled(&entries);
    cJSON* root = cJSON_CreateObject();
    cJSON* arr = cJSON_CreateArray();
    for (const auto& e : entries) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "local_id", e.local_id);
        cJSON_AddStringToObject(o, "display_name", e.display_name);
        if (e.immich_person_id[0]) {
            cJSON_AddStringToObject(o, "immich_person_id", e.immich_person_id);
        }
        if (e.immich_asset_id[0]) {
            cJSON_AddStringToObject(o, "immich_asset_id", e.immich_asset_id);
        }
        cJSON_AddBoolToObject(o, "name_pending", e.name_pending);
        cJSON_AddNumberToObject(o, "updated_at", e.updated_at);
        if (e.alias_count > 0) {
            cJSON* aliases = cJSON_CreateArray();
            for (int i = 0; i < e.alias_count; ++i) {
                cJSON_AddItemToArray(aliases, cJSON_CreateNumber(e.aliases[i]));
            }
            cJSON_AddItemToObject(o, "aliases", aliases);
        }
        cJSON_AddItemToArray(arr, o);
    }
    cJSON_AddItemToObject(root, "entries", arr);
    cJSON_AddNumberToObject(root, "count", static_cast<double>(entries.size()));
    if (include_live) {
        DeepDogFaceSnapshot snap{};
        DeepDogFaceControlCopySnapshot(&snap);
        cJSON* live = cJSON_CreateObject();
        cJSON_AddNumberToObject(live, "n", snap.count);
        cJSON_AddNumberToObject(live, "primary_local_id", snap.primary_local_id);
        cJSON_AddItemToObject(root, "live", live);
    }
    char* printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return "{}";
    }
    std::string out(printed);
    cJSON_free(printed);
    return out;
}

}  // namespace

void RegisterFaceMcpTools(McpServer& mcp_server) {
    mcp_server.AddTool(
        "self.face.set_mode",
        "Set face detect/recognize mode. detect=on/off; recognize=on/off; optional pipeline live|identity, interval_ms.",
        PropertyList({Property("detect", kPropertyTypeBoolean, true),
                      Property("recognize", kPropertyTypeBoolean, true),
                      Property("pipeline", kPropertyTypeString, std::string("live")),
                      Property("interval_ms", kPropertyTypeInteger, 500, 200, 5000)}),
        [](const PropertyList& p) -> ReturnValue {
            DeepDogFaceControlSetDetectionEnabled(p["detect"].value<bool>());
            DeepDogFaceControlSetRecognitionEnabled(p["recognize"].value<bool>());
            const std::string pipe = p["pipeline"].value<std::string>();
            if (pipe == "identity") {
                DeepDogFaceControlSetPipeline(DeepDogFacePipeline::Identity);
            } else {
                DeepDogFaceControlSetPipeline(DeepDogFacePipeline::Live);
            }
            DeepDogFaceControlSetDetectIntervalMs(p["interval_ms"].value<int>());
            return true;
        });

    mcp_server.AddTool(
        "self.face.manage",
        "Manage face gallery: action=clear_all|delete|rename|merge|refresh_immich; local_id; target_id; name.",
        PropertyList({Property("action", kPropertyTypeString, std::string("")),
                      Property("local_id", kPropertyTypeInteger, 0, 0, DEEP_DOG_FACE_RECOG_MAX),
                      Property("target_id", kPropertyTypeInteger, 0, 0, DEEP_DOG_FACE_RECOG_MAX),
                      Property("name", kPropertyTypeString, std::string(""))}),
        [](const PropertyList& p) -> ReturnValue {
            const std::string action = p["action"].value<std::string>();
            const int local_id = p["local_id"].value<int>();
            const int target_id = p["target_id"].value<int>();
            if (action.empty()) {
                return std::string("missing action");
            }
            if (action == "clear_all") {
                return DeepDogFaceControlClearAll();
            }
            if (action == "delete") {
                return DeepDogFaceControlDeleteOne(local_id);
            }
            if (action == "rename") {
                const std::string name = p["name"].value<std::string>();
                return DeepDogFaceControlRename(local_id, name.c_str());
            }
            if (action == "merge") {
                return DeepDogFaceControlMergeAlias(local_id, target_id);
            }
            if (action == "refresh_immich") {
                return DeepDogFaceControlRefreshImmich(local_id);
            }
            return std::string("unknown action");
        });

    mcp_server.AddTool(
        "self.face.list",
        "List enrolled canonical faces; include_live=true adds current frame summary.",
        PropertyList({Property("include_live", kPropertyTypeBoolean, false)}),
        [](const PropertyList& p) -> ReturnValue {
            return EnrolledListJson(p["include_live"].value<bool>());
        });

    ESP_LOGI(TAG, "registered face MCP tools");
}

#endif
