#pragma once

#include "mqtt/mqtt_config.h"

#include <cJSON.h>
#include <cstring>
#include <ctime>

#include <string>

/**
 * MQTT 载荷信封（跨端契约 01-mqtt-contract §4）。
 *
 * 统一报文体：
 *   { "schema": "<domain>.<kind>.v1", "ts_iso": "...", "device_id": "...",
 *     "seq": n?, "msg_id"?, "corr_id"?, "code"?, "data": { ...业务字段... } }
 *
 * 各模块把已有业务 JSON（data）经此处包装后再发布，业务字段不再平铺在根。
 */
namespace deep_dog_envelope {

/** UTC ISO8601（秒精度，带 Z），如 2026-10-15T08:42:54Z */
inline std::string IsoTsNow() {
    time_t t = time(nullptr);
    struct tm tm_utc {};
    gmtime_r(&t, &tm_utc);
    char buf[40];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", tm_utc.tm_year + 1900,
             tm_utc.tm_mon + 1, tm_utc.tm_mday, tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
    return buf;
}

/**
 * 把业务 JSON（data）包成信封，返回序列化字符串。
 * **接管 data 所有权**（内部随信封一起释放），调用方不要再 Delete。
 *
 * @param data      业务字段对象（可为 nullptr → 返回空串）
 * @param schema    报文版本，如 "deep-trace.status.v1"
 * @param device_id 设备唯一标识（MAC 紧凑串）
 */
inline std::string Wrap(cJSON* data, const char* schema, const std::string& device_id) {
    if (!data) {
        return std::string();
    }
    cJSON* env = cJSON_CreateObject();
    if (!env) {
        cJSON_Delete(data);
        return std::string();
    }
    const std::string ts = IsoTsNow();
    cJSON_AddStringToObject(env, "schema", schema ? schema : "deep-trace.status.v1");
    cJSON_AddStringToObject(env, "ts_iso", ts.c_str());
    cJSON_AddStringToObject(env, "device_id", device_id.c_str());
    cJSON_AddItemToObject(env, "data", data);  // 接管所有权
    char* printed = cJSON_PrintUnformatted(env);
    cJSON_Delete(env);
    if (!printed) {
        return std::string();
    }
    std::string out(printed);
    cJSON_free(printed);
    return out;
}

/**
 * 解析**入站**报文：统一信封时返回其 ``data``（业务字段），旧扁平则返回根对象。
 * **返回值由调用方 cJSON_Delete 释放**；解析失败返回 nullptr。
 */
inline cJSON* ParseBusiness(const std::string& payload) {
    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return nullptr;
    }
    const cJSON* schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (cJSON_IsString(schema) && cJSON_IsObject(data) &&
        strncmp(schema->valuestring, "deep-trace.", sizeof("deep-trace.") - 1) == 0) {
        cJSON* copy = cJSON_Duplicate(data, 1);
        cJSON_Delete(root);
        return copy;
    }
    return root;
}

/** schema 常量 */
inline const char* SchemaInfo() { return "deep-trace.info.v1"; }
inline const char* SchemaStatus() { return "deep-trace.status.v1"; }
inline const char* SchemaEvent() { return "deep-trace.event.v1"; }
inline const char* SchemaResult() { return "deep-trace.result.v1"; }
inline const char* SchemaAck() { return "deep-trace.cmd.v1.ack"; }

}  // namespace deep_dog_envelope
