#include "http-server/datapoint_http.h"

#include "mqtt/datapoint_registry.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <cstring>
#include <ctime>
#include <string>

#define TAG "dp_http"

namespace {

// ============================================================
// 工具：发送 JSON 响应
// ============================================================

esp_err_t SendJsonResponse(httpd_req_t* req, cJSON* json, int status_code = 200) {
    httpd_resp_set_status(req, status_code == 200 ? "200 OK" :
                            status_code == 400 ? "400 Bad Request" :
                            status_code == 404 ? "404 Not Found" :
                            status_code == 403 ? "403 Forbidden" :
                            status_code == 500 ? "500 Internal Server Error" : "200 OK");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char* buf = cJSON_PrintUnformatted(json);
    if (!buf) {
        cJSON_Delete(json);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    esp_err_t err = httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    cJSON_free(buf);
    cJSON_Delete(json);
    return err;
}

esp_err_t SendErrorResponse(httpd_req_t* req, const char* code, const char* message, int status_code) {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(obj, "ok", false);
    cJSON* err = cJSON_AddObjectToObject(obj, "error");
    cJSON_AddStringToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", message);
    return SendJsonResponse(req, obj, status_code);
}

// ============================================================
// 值转 JSON
// ============================================================

cJSON* ValueToJson(DpType type, const DpValue& val) {
    switch (type) {
        case DP_TYPE_BOOL:
            return cJSON_CreateBool(val.b);
        case DP_TYPE_INT:
            return cJSON_CreateNumber(static_cast<double>(val.i));
        case DP_TYPE_FLOAT:
            return cJSON_CreateNumber(static_cast<double>(val.f));
        case DP_TYPE_STRING:
        case DP_TYPE_ENUM:
            return cJSON_CreateString(val.s ? val.s : "");
        default:
            return cJSON_CreateNull();
    }
}

// ============================================================
// 构造带时间戳的值对象
// ============================================================

cJSON* BuildValueObject(DpType type, const DpValue& val) {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddItemToObject(obj, "value", ValueToJson(type, val));
    cJSON_AddNumberToObject(obj, "ts", static_cast<double>(time(nullptr)));
    return obj;
}

// ============================================================
// GET /api/datapoints/schema
// ============================================================

esp_err_t SchemaHandler(httpd_req_t* req) {
    DataPointRegistry& reg = DataPointRegistry::Instance();

    cJSON* schema = reg.BuildSchemaJson();
    if (!schema) {
        return SendErrorResponse(req, "SCHEMA_ERROR", "Failed to build schema", 500);
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddItemToObject(resp, "data", schema);
    cJSON_AddNullToObject(resp, "error");
    return SendJsonResponse(req, resp);
}

// ============================================================
// GET /api/datapoints/module/<name>
// ============================================================

esp_err_t ModuleHandler(httpd_req_t* req) {
    // 从 URL 中提取模块名
    // URL: /api/datapoints/module/<name>
    const char* prefix = "/api/datapoints/module/";
    size_t prefix_len = strlen(prefix);
    const char* uri = req->uri;

    if (strlen(uri) <= prefix_len) {
        return SendErrorResponse(req, "MISSING_MODULE", "Module name required", 400);
    }

    std::string module_name(uri + prefix_len);

    const DataModuleDef* mod = DataPointRegistry::Instance().FindModule(module_name.c_str());
    if (!mod) {
        return SendErrorResponse(req, "UNKNOWN_MODULE", ("Module not found: " + module_name).c_str(), 404);
    }

    cJSON* data = cJSON_CreateObject();

    for (size_t i = 0; i < mod->point_count; ++i) {
        const DataPointDef& dp = mod->points[i];
        if (dp.access == DP_ACCESS_WO) continue;

        DpValue val;
        DpError err = DataPointRegistry::Instance().ReadValue(dp.id, val);
        if (err != DP_ERR_OK) continue;

        const char* point_name = dp.id;
        const char* last_dot = strrchr(dp.id, '.');
        if (last_dot) point_name = last_dot + 1;

        cJSON_AddItemToObject(data, point_name, BuildValueObject(dp.type, val));
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddItemToObject(resp, "data", data);
    cJSON_AddNullToObject(resp, "error");
    return SendJsonResponse(req, resp);
}

// ============================================================
// GET /api/datapoints/point/<id>
//
// 注意：<id> 中可能包含点号（如 "led.brightness"），
// HTTP URL 中直接编码为 /api/datapoints/point/led.brightness 是可行的。
// ============================================================

esp_err_t PointHandler(httpd_req_t* req) {
    const char* prefix = "/api/datapoints/point/";
    size_t prefix_len = strlen(prefix);
    const char* uri = req->uri;

    if (strlen(uri) <= prefix_len) {
        return SendErrorResponse(req, "MISSING_POINT", "Point ID required", 400);
    }

    std::string point_id(uri + prefix_len);

    const DataPointDef* dp = DataPointRegistry::Instance().FindPoint(point_id.c_str());
    if (!dp) {
        return SendErrorResponse(req, "UNKNOWN_POINT", ("Point not found: " + point_id).c_str(), 404);
    }

    if (dp->access == DP_ACCESS_WO) {
        return SendErrorResponse(req, "WRITE_ONLY", "This point is write-only", 403);
    }

    DpValue val;
    DpError err = DataPointRegistry::Instance().ReadValue(point_id.c_str(), val);
    if (err != DP_ERR_OK) {
        return SendErrorResponse(req, "READ_FAILED", "Failed to read point", 500);
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddItemToObject(resp, "data", BuildValueObject(dp->type, val));
    cJSON_AddNullToObject(resp, "error");
    return SendJsonResponse(req, resp);
}

// ============================================================
// POST /api/datapoints/set — 写入单个点
//   body: { "id": "led.brightness", "value": 200 }
// ============================================================

esp_err_t SetHandler(httpd_req_t* req) {
    // 读取 body
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return SendErrorResponse(req, "EMPTY_BODY", "Empty request body", 400);
    }
    buf[ret] = '\0';

    cJSON* root = cJSON_Parse(buf);
    if (!root) {
        return SendErrorResponse(req, "INVALID_JSON", "Invalid JSON", 400);
    }

    const cJSON* id_json = cJSON_GetObjectItem(root, "id");
    const cJSON* val_json = cJSON_GetObjectItem(root, "value");

    if (!cJSON_IsString(id_json) || !val_json) {
        cJSON_Delete(root);
        return SendErrorResponse(req, "INVALID_PARAMS", "Missing 'id' or 'value'", 400);
    }

    const char* point_id = id_json->valuestring;
    const DataPointDef* dp = DataPointRegistry::Instance().FindPoint(point_id);
    if (!dp) {
        cJSON_Delete(root);
        return SendErrorResponse(req, "UNKNOWN_POINT", ("Point not found: " + std::string(point_id)).c_str(), 404);
    }

    if (dp->access == DP_ACCESS_RO) {
        cJSON_Delete(root);
        return SendErrorResponse(req, "READ_ONLY", "This point is read-only", 403);
    }

    // 解析 value
    DpValue val{};
    bool valid = false;

    switch (dp->type) {
        case DP_TYPE_BOOL:
            if (cJSON_IsBool(val_json)) {
                val.b = cJSON_IsTrue(val_json);
                valid = true;
            }
            break;
        case DP_TYPE_INT:
            if (cJSON_IsNumber(val_json)) {
                val.i = static_cast<int32_t>(val_json->valuedouble);
                valid = true;
            }
            break;
        case DP_TYPE_FLOAT:
            if (cJSON_IsNumber(val_json)) {
                val.f = static_cast<float>(val_json->valuedouble);
                valid = true;
            }
            break;
        case DP_TYPE_STRING:
        case DP_TYPE_ENUM:
            if (cJSON_IsString(val_json)) {
                val.s = val_json->valuestring;
                valid = true;
            }
            break;
    }

    if (!valid) {
        cJSON_Delete(root);
        return SendErrorResponse(req, "TYPE_MISMATCH", "Value type mismatch", 400);
    }

    DpError err = DataPointRegistry::Instance().WriteValue(point_id, val);
    cJSON_Delete(root);

    if (err != DP_ERR_OK) {
        const char* code = "WRITE_FAILED";
        int status = 400;
        switch (err) {
            case DP_ERR_OUT_OF_RANGE: code = "OUT_OF_RANGE"; break;
            case DP_ERR_INVALID_ENUM: code = "INVALID_ENUM"; break;
            case DP_ERR_TYPE_MISMATCH: code = "TYPE_MISMATCH"; break;
            case DP_ERR_READ_ONLY: code = "READ_ONLY"; status = 403; break;
            default: code = "WRITE_FAILED"; status = 500; break;
        }
        return SendErrorResponse(req, code, "Write failed", status);
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddNullToObject(resp, "data");
    cJSON_AddNullToObject(resp, "error");
    return SendJsonResponse(req, resp);
}

// ============================================================
// 解析 value 为目标数据点可接受的 DpValue（类型校验）
// ============================================================

bool ParseValueForPoint(const DataPointDef* dp, const cJSON* val_json, DpValue* out) {
    if (!dp || !val_json || !out) return false;
    switch (dp->type) {
        case DP_TYPE_BOOL:
            if (cJSON_IsBool(val_json)) { out->b = cJSON_IsTrue(val_json); return true; }
            return false;
        case DP_TYPE_INT:
            if (cJSON_IsNumber(val_json)) { out->i = static_cast<int32_t>(val_json->valuedouble); return true; }
            return false;
        case DP_TYPE_FLOAT:
            if (cJSON_IsNumber(val_json)) { out->f = static_cast<float>(val_json->valuedouble); return true; }
            return false;
        case DP_TYPE_STRING:
        case DP_TYPE_ENUM:
            if (cJSON_IsString(val_json)) { out->s = val_json->valuestring; return true; }
            return false;
        default:
            return false;
    }
}

/** DpError → HTTP 状态码 */
int DpErrorStatus(DpError err) {
    switch (err) {
        case DP_ERR_OUT_OF_RANGE:
        case DP_ERR_INVALID_ENUM:
        case DP_ERR_TYPE_MISMATCH:
            return 400;
        case DP_ERR_READ_ONLY:
            return 403;
        case DP_ERR_UNKNOWN_POINT:
            return 404;
        default:
            return 500;
    }
}

/** DpError → 错误码字符串 */
const char* DpErrorCode(DpError err) {
    switch (err) {
        case DP_ERR_OUT_OF_RANGE:   return "OUT_OF_RANGE";
        case DP_ERR_INVALID_ENUM:   return "INVALID_ENUM";
        case DP_ERR_TYPE_MISMATCH:  return "TYPE_MISMATCH";
        case DP_ERR_READ_ONLY:      return "READ_ONLY";
        case DP_ERR_UNKNOWN_POINT:  return "UNKNOWN_POINT";
        default:                    return "WRITE_FAILED";
    }
}

// ============================================================
// POST /api/datapoints/batch — 批量读取
//   body: { "points": ["device.online", "system.uptime_s", ...] }   （兼容 "ids"）
//   响应: { ok:true, data:{ "<id>": {value, ts}, ... }, error:null }
// ============================================================

esp_err_t BatchReadHandler(httpd_req_t* req) {
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return SendErrorResponse(req, "EMPTY_BODY", "Empty request body", 400);
    }
    if (ret == static_cast<int>(sizeof(buf)) - 1) {
        return SendErrorResponse(req, "PAYLOAD_TOO_LARGE", "Batch body too large", 413);
    }
    buf[ret] = '\0';

    cJSON* root = cJSON_Parse(buf);
    if (!root) {
        return SendErrorResponse(req, "INVALID_JSON", "Invalid JSON", 400);
    }

    const cJSON* arr = cJSON_GetObjectItem(root, "points");
    if (!cJSON_IsArray(arr)) {
        arr = cJSON_GetObjectItem(root, "ids");
    }
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) == 0) {
        cJSON_Delete(root);
        return SendErrorResponse(req, "INVALID_PARAMS", "Missing 'points' array", 400);
    }

    DataPointRegistry& reg = DataPointRegistry::Instance();
    cJSON* data = cJSON_CreateObject();
    const int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n; ++i) {
        const cJSON* it = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsString(it)) continue;
        const char* pid = it->valuestring;
        const DataPointDef* dp = reg.FindPoint(pid);
        if (!dp) {
            cJSON_Delete(data);
            cJSON_Delete(root);
            return SendErrorResponse(req, "UNKNOWN_POINT",
                                     ("Point not found: " + std::string(pid)).c_str(), 404);
        }
        DpValue v{};
        if (reg.ReadValue(pid, v) != DP_ERR_OK) {
            cJSON_Delete(data);
            cJSON_Delete(root);
            return SendErrorResponse(req, "READ_FAILED",
                                     ("Read failed: " + std::string(pid)).c_str(), 500);
        }
        cJSON_AddItemToObject(data, pid, BuildValueObject(dp->type, v));
    }

    cJSON* resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddItemToObject(resp, "data", data);
    cJSON_AddNullToObject(resp, "error");
    cJSON_Delete(root);
    return SendJsonResponse(req, resp);
}

// ============================================================
// POST /api/datapoints/batch_set — 批量写入（最好努力：逐项写入）
//   body: { "points": [ {"id":"servo.0.angle","value":90}, ... ] }
//   全成功 → ok:true；有失败 → ok:false + error{code}，并在 data.failed 列出明细
// ============================================================

esp_err_t BatchSetHandler(httpd_req_t* req) {
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return SendErrorResponse(req, "EMPTY_BODY", "Empty request body", 400);
    }
    if (ret == static_cast<int>(sizeof(buf)) - 1) {
        return SendErrorResponse(req, "PAYLOAD_TOO_LARGE", "Batch body too large", 413);
    }
    buf[ret] = '\0';

    cJSON* root = cJSON_Parse(buf);
    if (!root) {
        return SendErrorResponse(req, "INVALID_JSON", "Invalid JSON", 400);
    }

    const cJSON* arr = cJSON_GetObjectItem(root, "points");
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) == 0) {
        cJSON_Delete(root);
        return SendErrorResponse(req, "INVALID_PARAMS", "Missing 'points' array", 400);
    }

    DataPointRegistry& reg = DataPointRegistry::Instance();
    cJSON* failed = cJSON_CreateArray();
    int applied = 0;
    DpError first_err = DP_ERR_OK;

    const int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n; ++i) {
        const cJSON* item = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsObject(item)) continue;
        const cJSON* id_j = cJSON_GetObjectItem(item, "id");
        const cJSON* val_j = cJSON_GetObjectItem(item, "value");
        if (!cJSON_IsString(id_j) || !val_j) {
            continue;
        }
        const char* pid = id_j->valuestring;
        const DataPointDef* dp = reg.FindPoint(pid);
        if (!dp) {
            if (first_err == DP_ERR_OK) first_err = DP_ERR_UNKNOWN_POINT;
            cJSON* f = cJSON_CreateObject();
            cJSON_AddStringToObject(f, "id", pid);
            cJSON_AddStringToObject(f, "code", "UNKNOWN_POINT");
            cJSON_AddItemToArray(failed, f);
            continue;
        }
        DpValue v{};
        if (!ParseValueForPoint(dp, val_j, &v)) {
            if (first_err == DP_ERR_OK) first_err = DP_ERR_TYPE_MISMATCH;
            cJSON* f = cJSON_CreateObject();
            cJSON_AddStringToObject(f, "id", pid);
            cJSON_AddStringToObject(f, "code", "TYPE_MISMATCH");
            cJSON_AddItemToArray(failed, f);
            continue;
        }
        const DpError err = reg.WriteValue(pid, v);
        if (err != DP_ERR_OK) {
            if (first_err == DP_ERR_OK) first_err = err;
            cJSON* f = cJSON_CreateObject();
            cJSON_AddStringToObject(f, "id", pid);
            cJSON_AddStringToObject(f, "code", DpErrorCode(err));
            cJSON_AddItemToArray(failed, f);
            continue;
        }
        ++applied;
    }

    cJSON* data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "applied", applied);
    cJSON_AddItemToObject(data, "failed", failed);

    cJSON* resp = cJSON_CreateObject();
    const bool all_ok = (first_err == DP_ERR_OK);
    cJSON_AddBoolToObject(resp, "ok", all_ok);
    cJSON_AddItemToObject(resp, "data", data);
    if (all_ok) {
        cJSON_AddNullToObject(resp, "error");
    } else {
        cJSON* err = cJSON_AddObjectToObject(resp, "error");
        cJSON_AddStringToObject(err, "code", DpErrorCode(first_err));
        cJSON_AddStringToObject(err, "message", "Some points failed (see data.failed)");
    }
    cJSON_Delete(root);
    return SendJsonResponse(req, resp, all_ok ? 200 : DpErrorStatus(first_err));
}

}  // namespace

// ============================================================
// 注册所有端点
// ============================================================

bool RegisterDataPointHttpEndpoints(httpd_handle_t server) {
    if (!server) return false;

    httpd_uri_t uri_schema = {
        .uri = "/api/datapoints/schema",
        .method = HTTP_GET,
        .handler = SchemaHandler,
        .user_ctx = nullptr
    };

    // module/point 用通配符注册（httpd 支持 URI 通配）
    // 这里简化：直接注册前缀匹配不支持，就用固定 handler + 路径解析
    httpd_uri_t uri_module = {
        .uri = "/api/datapoints/module/*",
        .method = HTTP_GET,
        .handler = ModuleHandler,
        .user_ctx = nullptr
    };

    httpd_uri_t uri_point = {
        .uri = "/api/datapoints/point/*",
        .method = HTTP_GET,
        .handler = PointHandler,
        .user_ctx = nullptr
    };

    httpd_uri_t uri_set = {
        .uri = "/api/datapoints/set",
        .method = HTTP_POST,
        .handler = SetHandler,
        .user_ctx = nullptr
    };

    esp_err_t err = ESP_OK;
    httpd_uri_t uri_batch = {
        .uri = "/api/datapoints/batch",
        .method = HTTP_POST,
        .handler = BatchReadHandler,
        .user_ctx = nullptr
    };

    httpd_uri_t uri_batch_set = {
        .uri = "/api/datapoints/batch_set",
        .method = HTTP_POST,
        .handler = BatchSetHandler,
        .user_ctx = nullptr
    };

    err |= httpd_register_uri_handler(server, &uri_schema);
    err |= httpd_register_uri_handler(server, &uri_module);
    err |= httpd_register_uri_handler(server, &uri_point);
    err |= httpd_register_uri_handler(server, &uri_set);
    err |= httpd_register_uri_handler(server, &uri_batch);
    err |= httpd_register_uri_handler(server, &uri_batch_set);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register datapoint HTTP endpoints");
        return false;
    }

    ESP_LOGI(TAG, "registered /api/datapoints/* endpoints (schema, module, point, set)");
    return true;
}
