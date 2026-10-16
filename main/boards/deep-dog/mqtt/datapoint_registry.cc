#include "mqtt/datapoint_registry.h"

#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_topics.h"
#include "mqtt/modules/envelope.h"

#include <cJSON.h>
#include <esp_log.h>
#include <climits>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <map>

#define TAG "dp_registry"

// ============================================================
// 内部实现
// ============================================================

struct DataPointRegistry::Impl {
    struct ModuleEntry {
        DataModuleDef def;
        std::vector<DataPointDef> points;  // 展开后的点（含子实例）
        std::string module_name;
        // 子实例展开时，持久化 "module.sub_id.point_name" 字符串；
        // 已 reserve 到精确容量，故 c_str() 指针在整个生命周期稳定。
        std::vector<std::string> id_storage;
        // 持久化的 sub_id 字符串（作为展开点的默认读写上下文）
        std::vector<std::string> sub_storage;
    };

    std::vector<ModuleEntry> modules;
    std::map<std::string, size_t> point_index;  // point_id -> module index
    std::map<std::string, size_t> module_index;  // module_name -> index in modules

    // 上次发布的值（用于 on_change 死区检测）
    struct LastValue {
        DpType type;
        DpValue value;
        bool valid = false;
    };
    std::map<std::string, LastValue> last_values;
};

// ============================================================
// 单例
// ============================================================

DataPointRegistry& DataPointRegistry::Instance() {
    static DataPointRegistry instance;
    return instance;
}

DataPointRegistry::DataPointRegistry() : impl_(new Impl()) {}

DataPointRegistry::~DataPointRegistry() {
    delete impl_;
}

// ============================================================
// 模块注册
// ============================================================

bool DataPointRegistry::RegisterModule(const DataModuleDef& module_def) {
    if (!module_def.name || !module_def.points || module_def.point_count == 0) {
        ESP_LOGE(TAG, "RegisterModule: invalid args (name=%s)", module_def.name ? module_def.name : "(null)");
        return false;
    }

    // 检查是否已注册
    auto it = impl_->module_index.find(module_def.name);
    if (it != impl_->module_index.end()) {
        ESP_LOGW(TAG, "RegisterModule: module '%s' already registered", module_def.name);
        return false;
    }

    Impl::ModuleEntry entry;
    entry.def = module_def;
    entry.module_name = module_def.name;

    // 直接复制点定义（无子实例时）
    entry.points.reserve(module_def.point_count);
    for (size_t i = 0; i < module_def.point_count; ++i) {
        entry.points.push_back(module_def.points[i]);
        const std::string pid = module_def.points[i].id;
        impl_->point_index[pid] = impl_->modules.size();
    }

    impl_->module_index[module_def.name] = impl_->modules.size();
    impl_->modules.push_back(std::move(entry));

    ESP_LOGI(TAG, "registered module '%s' with %zu points", module_def.name, module_def.point_count);
    return true;
}

bool DataPointRegistry::RegisterMultiInstanceModule(const DataModuleDef& module_template,
                                                    const char** sub_ids, size_t sub_count) {
    if (!module_template.name || !module_template.points || module_template.point_count == 0 ||
        !sub_ids || sub_count == 0) {
        ESP_LOGE(TAG, "RegisterMultiInstanceModule: invalid args");
        return false;
    }

    auto it = impl_->module_index.find(module_template.name);
    if (it != impl_->module_index.end()) {
        ESP_LOGW(TAG, "RegisterMultiInstanceModule: module '%s' already registered", module_template.name);
        return false;
    }

    Impl::ModuleEntry entry;
    entry.def = module_template;
    entry.def.has_sub_instances = true;
    entry.module_name = module_template.name;

    // 先按精确上限 reserve，保证 id_storage 不再 reallocate，
    // 从而 entry.points[i].id 指向的字符串指针在整个生命周期内稳定。
    entry.id_storage.reserve(module_template.point_count * sub_count);
    entry.sub_storage.reserve(sub_count);

    for (size_t si = 0; si < sub_count; ++si) {
        const char* sid = sub_ids[si];
        if (!sid) continue;

        // 持久化 sub_id（reserve 后地址稳定），作为展开点的默认 ctx
        entry.sub_storage.emplace_back(sid);
        const char* stable_sub = entry.sub_storage.back().c_str();

        for (size_t pi = 0; pi < module_template.point_count; ++pi) {
            const DataPointDef& tmpl = module_template.points[pi];
            DataPointDef dp = tmpl;

            // 取模板点名的最后一段（去掉 module 前缀）
            const char* last_dot = strrchr(tmpl.id ? tmpl.id : "", '.');
            const char* point_name = last_dot ? last_dot + 1 : tmpl.id;

            // 构造展开后的 id: module.sub_id.point_name
            char buf[160];
            snprintf(buf, sizeof(buf), "%s.%s.%s", module_template.name, sid, point_name);
            entry.id_storage.emplace_back(buf);

            dp.id = entry.id_storage.back().c_str();  // 指向持久化字符串
            dp.sub_id = nullptr;                      // sub_id 已并入 id，避免悬垂指针
            // 默认把 sub_id 字符串作为读写上下文（回调可用 atoi((const char*)ctx) 取实例号）；
            // 若模板点已显式指定 ctx，则尊重模板值。
            if (!dp.read_ctx) {
                dp.read_ctx = const_cast<char*>(stable_sub);
            }
            if (!dp.write_ctx) {
                dp.write_ctx = const_cast<char*>(stable_sub);
            }
            entry.points.push_back(dp);
        }
    }

    if (entry.points.empty()) {
        ESP_LOGE(TAG, "RegisterMultiInstanceModule: no valid sub-instance expanded for '%s'",
                 module_template.name);
        return false;
    }

    const size_t mod_idx = impl_->modules.size();
    for (const auto& dp : entry.points) {
        if (dp.id) {
            impl_->point_index[dp.id] = mod_idx;
        }
    }
    impl_->module_index[module_template.name] = mod_idx;
    impl_->modules.push_back(std::move(entry));

    ESP_LOGI(TAG, "registered multi-instance module '%s': %zu sub x %zu points = %zu points",
             module_template.name, sub_count, module_template.point_count,
             impl_->modules.back().points.size());
    return true;
}

// ============================================================
// 查询
// ============================================================

const DataPointDef* DataPointRegistry::FindPoint(const char* point_id) const {
    if (!point_id) return nullptr;

    auto it = impl_->point_index.find(point_id);
    if (it == impl_->point_index.end()) {
        return nullptr;
    }

    size_t mod_idx = it->second;
    if (mod_idx >= impl_->modules.size()) return nullptr;

    const auto& entry = impl_->modules[mod_idx];
    for (const auto& dp : entry.points) {
        if (dp.id && strcmp(dp.id, point_id) == 0) {
            return &dp;
        }
    }
    return nullptr;
}

size_t DataPointRegistry::ModuleCount() const {
    return impl_->modules.size();
}

const DataModuleDef* DataPointRegistry::GetModule(size_t idx) const {
    if (idx >= impl_->modules.size()) return nullptr;
    return &impl_->modules[idx].def;
}

const DataModuleDef* DataPointRegistry::FindModule(const char* module_name) const {
    if (!module_name) return nullptr;
    auto it = impl_->module_index.find(module_name);
    if (it == impl_->module_index.end()) return nullptr;
    return &impl_->modules[it->second].def;
}

// ============================================================
// 读取值
// ============================================================

static DpError ReadPointValue(const DataPointDef* dp, DpValue& out) {
    if (!dp) return DP_ERR_UNKNOWN_POINT;

    switch (dp->type) {
        case DP_TYPE_BOOL:
            if (dp->access == DP_ACCESS_WO) return DP_ERR_WRITE_ONLY;
            if (dp->read_bool) {
                out.b = dp->read_bool(dp->read_ctx);
            } else {
                out.b = false;
            }
            break;
        case DP_TYPE_INT:
            if (dp->access == DP_ACCESS_WO) return DP_ERR_WRITE_ONLY;
            if (dp->read_int) {
                out.i = dp->read_int(dp->read_ctx);
            } else {
                out.i = 0;
            }
            break;
        case DP_TYPE_FLOAT:
            if (dp->access == DP_ACCESS_WO) return DP_ERR_WRITE_ONLY;
            if (dp->read_float) {
                out.f = dp->read_float(dp->read_ctx);
            } else {
                out.f = 0.0f;
            }
            break;
        case DP_TYPE_STRING:
        case DP_TYPE_ENUM:
            if (dp->access == DP_ACCESS_WO) return DP_ERR_WRITE_ONLY;
            if (dp->read_string) {
                out.s = dp->read_string(dp->read_ctx);
            } else {
                out.s = "";
            }
            break;
        default:
            return DP_ERR_TYPE_MISMATCH;
    }
    return DP_ERR_OK;
}

DpError DataPointRegistry::ReadValue(const char* point_id, DpValue& out_value) const {
    const DataPointDef* dp = FindPoint(point_id);
    if (!dp) return DP_ERR_UNKNOWN_POINT;
    return ReadPointValue(dp, out_value);
}

// ============================================================
// 写入值
// ============================================================

static bool ValidateEnumValue(const DataPointDef* dp, const char* value) {
    if (!dp->enum_values || !value) return false;
    for (size_t i = 0; dp->enum_values[i].value != nullptr; ++i) {
        if (strcmp(dp->enum_values[i].value, value) == 0) {
            return true;
        }
    }
    return false;
}

static DpError ValidateAndWrite(const DataPointDef* dp, const DpValue& value) {
    if (dp->access == DP_ACCESS_RO) return DP_ERR_READ_ONLY;

    bool write_ok = false;

    switch (dp->type) {
        case DP_TYPE_BOOL:
            if (dp->write_bool) {
                write_ok = dp->write_bool(dp->write_ctx, value.b);
            }
            break;
        case DP_TYPE_INT: {
            int32_t v = value.i;
            // 范围校验
            int32_t min_i = static_cast<int32_t>(dp->min_val);
            int32_t max_i = static_cast<int32_t>(dp->max_val);
            if (dp->min_val > static_cast<float>(INT32_MIN)) {
                if (v < min_i) return DP_ERR_OUT_OF_RANGE;
            }
            if (dp->max_val < static_cast<float>(INT32_MAX)) {
                if (v > max_i) return DP_ERR_OUT_OF_RANGE;
            }
            if (dp->write_int) {
                write_ok = dp->write_int(dp->write_ctx, v);
            }
            break;
        }
        case DP_TYPE_FLOAT: {
            float v = value.f;
            // 范围校验（用 FLT_MAX/FLT_MIN 表示无界，这里简化用 isnan 判断）
            if (dp->min_val == dp->min_val && dp->min_val > -1e30f) {  // not NaN and not -inf-ish
                if (v < dp->min_val) return DP_ERR_OUT_OF_RANGE;
            }
            if (dp->max_val == dp->max_val && dp->max_val < 1e30f) {  // not NaN and not +inf-ish
                if (v > dp->max_val) return DP_ERR_OUT_OF_RANGE;
            }
            if (dp->write_float) {
                write_ok = dp->write_float(dp->write_ctx, v);
            }
            break;
        }
        case DP_TYPE_STRING:
            if (dp->write_string) {
                write_ok = dp->write_string(dp->write_ctx, value.s ? value.s : "");
            }
            break;
        case DP_TYPE_ENUM:
            if (!ValidateEnumValue(dp, value.s)) return DP_ERR_INVALID_ENUM;
            if (dp->write_string) {
                write_ok = dp->write_string(dp->write_ctx, value.s ? value.s : "");
            }
            break;
        default:
            return DP_ERR_TYPE_MISMATCH;
    }

    return write_ok ? DP_ERR_OK : DP_ERR_WRITE_FAILED;
}

DpError DataPointRegistry::WriteValue(const char* point_id, const DpValue& value) {
    const DataPointDef* dp = FindPoint(point_id);
    if (!dp) return DP_ERR_UNKNOWN_POINT;

    DpError err = ValidateAndWrite(dp, value);
    if (err == DP_ERR_OK) {
        MarkChanged(point_id);
    }
    return err;
}

// ============================================================
// 序列化到 JSON
// ============================================================

static void AddValueToJson(cJSON* obj, const char* key, const DataPointDef* dp, const DpValue& val) {
    if (!obj || !key || !dp) return;

    switch (dp->type) {
        case DP_TYPE_BOOL:
            cJSON_AddBoolToObject(obj, key, val.b);
            break;
        case DP_TYPE_INT:
            cJSON_AddNumberToObject(obj, key, static_cast<double>(val.i));
            break;
        case DP_TYPE_FLOAT:
            cJSON_AddNumberToObject(obj, key, static_cast<double>(val.f));
            break;
        case DP_TYPE_STRING:
        case DP_TYPE_ENUM:
            cJSON_AddStringToObject(obj, key, val.s ? val.s : "");
            break;
    }
}

bool DataPointRegistry::SerializeModuleStatus(const char* module_name, cJSON* target_obj) const {
    if (!module_name || !target_obj) return false;

    auto it = impl_->module_index.find(module_name);
    if (it == impl_->module_index.end()) {
        ESP_LOGW(TAG, "SerializeModuleStatus: module '%s' not found", module_name);
        return false;
    }

    const auto& entry = impl_->modules[it->second];
    int count = 0;
    const bool has_sub = entry.def.has_sub_instances;

    for (const auto& dp : entry.points) {
        if (dp.access == DP_ACCESS_WO) continue;  // 只写点不上行

        DpValue val;
        DpError err = ReadPointValue(&dp, val);
        if (err != DP_ERR_OK) continue;

        const std::string id = dp.id ? dp.id : "";
        const size_t last_dot = id.rfind('.');

        if (has_sub) {
            // 子实例：按 sub_id 分组为嵌套对象 { "0": {angle:..}, "1": {...} }
            const size_t first_dot = id.find('.');
            if (first_dot == std::string::npos || first_dot == last_dot) continue;
            const std::string sub = id.substr(first_dot + 1, last_dot - first_dot - 1);
            const std::string pname = id.substr(last_dot + 1);

            cJSON* grp = cJSON_GetObjectItem(target_obj, sub.c_str());
            if (!grp) {
                grp = cJSON_AddObjectToObject(target_obj, sub.c_str());
            }
            AddValueToJson(grp, pname.c_str(), &dp, val);
        } else {
            // 单实例：用 id 最后一段作 key（与原行为一致）
            const char* pname = (last_dot == std::string::npos) ? id.c_str() : id.c_str() + last_dot + 1;
            AddValueToJson(target_obj, pname, &dp, val);
        }
        ++count;
    }

    return count > 0;
}

int DataPointRegistry::AppendTelemetryPoints(cJSON* points_obj) const {
    if (!points_obj) {
        return 0;
    }
    int count = 0;
    for (const auto& entry : impl_->modules) {
        for (const auto& dp : entry.points) {
            if (!(dp.channels & DP_CH_TELEMETRY)) continue;
            if (dp.type != DP_TYPE_INT && dp.type != DP_TYPE_FLOAT) continue;

            DpValue val;
            if (ReadPointValue(&dp, val) != DP_ERR_OK) continue;

            // key = 点 id 最后一段（如 device.free_heap -> free_heap）
            const std::string id = dp.id ? dp.id : "";
            const size_t last_dot = id.rfind('.');
            const char* key =
                (last_dot == std::string::npos) ? id.c_str() : id.c_str() + last_dot + 1;

            const double num = (dp.type == DP_TYPE_FLOAT) ? static_cast<double>(val.f)
                                                          : static_cast<double>(val.i);
            if (dp.unit && dp.unit[0]) {
                cJSON* obj = cJSON_AddObjectToObject(points_obj, key);
                cJSON_AddNumberToObject(obj, "value", num);
                cJSON_AddStringToObject(obj, "unit", dp.unit);
            } else {
                cJSON_AddNumberToObject(points_obj, key, num);
            }
            ++count;
        }
    }
    return count;
}

// ============================================================
// Schema 导出
// ============================================================

static void AppendTypeEnum(cJSON* arr, const char* t) {
    cJSON_AddItemToArray(arr, cJSON_CreateString(t));
}

cJSON* DataPointRegistry::BuildSchemaJson() const {
    cJSON* schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "schema_version", "1.0.0");
    cJSON* modules_obj = cJSON_AddObjectToObject(schema, "modules");

    for (const auto& entry : impl_->modules) {
        cJSON* mod_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(mod_obj, "name", entry.def.display_name ? entry.def.display_name : entry.def.name);
        if (entry.def.description) {
            cJSON_AddStringToObject(mod_obj, "description", entry.def.description);
        }
        cJSON_AddBoolToObject(mod_obj, "has_sub_instances", entry.def.has_sub_instances);

        // 子实例模块：输出 instances 描述（type/count/ids），points 用模板点（去重）
        if (entry.def.has_sub_instances) {
            std::vector<std::string> subs;
            std::map<std::string, bool> sub_seen;
            for (const auto& dp : entry.points) {
                const std::string sid = dp.id ? dp.id : "";
                const size_t f = sid.find('.');
                const size_t l = sid.rfind('.');
                if (f == std::string::npos || f == l) continue;
                const std::string sub = sid.substr(f + 1, l - f - 1);
                if (!sub_seen[sub]) {
                    sub_seen[sub] = true;
                    subs.push_back(sub);
                }
            }
            cJSON* inst = cJSON_AddObjectToObject(mod_obj, "instances");
            cJSON_AddStringToObject(inst, "type", "numeric");
            cJSON_AddNumberToObject(inst, "count", static_cast<double>(subs.size()));
            cJSON* ids = cJSON_CreateArray();
            for (const auto& s : subs) {
                char* end = nullptr;
                const double n = strtod(s.c_str(), &end);
                if (end && *end == '\0' && !s.empty()) {
                    cJSON_AddItemToArray(ids, cJSON_CreateNumber(n));
                } else {
                    cJSON_AddItemToArray(ids, cJSON_CreateString(s.c_str()));
                }
            }
            cJSON_AddItemToObject(inst, "ids", ids);
        }

        cJSON* points_obj = cJSON_AddObjectToObject(mod_obj, "points");
        std::map<std::string, bool> emitted;

        for (const auto& dp : entry.points) {
            cJSON* p = cJSON_CreateObject();

            // 类型
            const char* type_str = "unknown";
            switch (dp.type) {
                case DP_TYPE_BOOL:   type_str = "bool"; break;
                case DP_TYPE_INT:    type_str = "int"; break;
                case DP_TYPE_FLOAT:  type_str = "float"; break;
                case DP_TYPE_STRING: type_str = "string"; break;
                case DP_TYPE_ENUM:   type_str = "enum"; break;
            }
            cJSON_AddStringToObject(p, "type", type_str);

            // 单位
            if (dp.unit && dp.unit[0]) {
                cJSON_AddStringToObject(p, "unit", dp.unit);
            }

            // 访问模式
            const char* access_str = "ro";
            switch (dp.access) {
                case DP_ACCESS_RO: access_str = "ro"; break;
                case DP_ACCESS_WO: access_str = "wo"; break;
                case DP_ACCESS_RW: access_str = "rw"; break;
            }
            cJSON_AddStringToObject(p, "access", access_str);

            // 通道
            cJSON* ch_arr = cJSON_CreateArray();
            if (dp.channels & DP_CH_MQTT)      cJSON_AddItemToArray(ch_arr, cJSON_CreateString("mqtt"));
            if (dp.channels & DP_CH_HTTP)      cJSON_AddItemToArray(ch_arr, cJSON_CreateString("http"));
            if (dp.channels & DP_CH_MCP)       cJSON_AddItemToArray(ch_arr, cJSON_CreateString("mcp"));
            if (dp.channels & DP_CH_TELEMETRY) cJSON_AddItemToArray(ch_arr, cJSON_CreateString("telemetry"));
            cJSON_AddItemToObject(p, "channels", ch_arr);

            // 数值范围
            if (dp.type == DP_TYPE_INT || dp.type == DP_TYPE_FLOAT) {
                if (dp.min_val > -1e30f && dp.min_val == dp.min_val) {
                    cJSON_AddNumberToObject(p, "min", static_cast<double>(dp.min_val));
                }
                if (dp.max_val < 1e30f && dp.max_val == dp.max_val) {
                    cJSON_AddNumberToObject(p, "max", static_cast<double>(dp.max_val));
                }
            }

            // 精度
            if (dp.type == DP_TYPE_FLOAT && dp.precision > 0) {
                cJSON_AddNumberToObject(p, "precision", dp.precision);
            }

            // 枚举值
            if (dp.type == DP_TYPE_ENUM && dp.enum_values) {
                cJSON* ev = cJSON_CreateArray();
                for (size_t i = 0; dp.enum_values[i].value; ++i) {
                    cJSON* e = cJSON_CreateObject();
                    cJSON_AddStringToObject(e, "value", dp.enum_values[i].value);
                    if (dp.enum_values[i].label) {
                        cJSON_AddStringToObject(e, "label", dp.enum_values[i].label);
                    }
                    cJSON_AddItemToArray(ev, e);
                }
                cJSON_AddItemToObject(p, "enum_values", ev);
            }

            // 描述
            if (dp.description) {
                cJSON_AddStringToObject(p, "description", dp.description);
            }

            // 提取点名称（用作 key）
            const char* point_name = dp.id;
            const char* last_dot = strrchr(dp.id ? dp.id : "", '.');
            if (last_dot) point_name = last_dot + 1;

            // 子实例模块：每个 point_name 仅输出一次（模板点）
            if (entry.def.has_sub_instances) {
                if (emitted[point_name]) {
                    cJSON_Delete(p);
                    continue;
                }
                emitted[point_name] = true;
            }

            cJSON_AddItemToObject(points_obj, point_name, p);
        }

        cJSON_AddItemToObject(modules_obj, entry.module_name.c_str(), mod_obj);
    }

    return schema;
}

// ============================================================
// 变化标记 & 发布
// ============================================================

void DataPointRegistry::MarkChanged(const char* point_id) {
    (void)point_id;
    // TODO: on_change 策略 + 死区检测 + 节流发布
    // 第一版简化：变化时直接强制发布整个模块
    if (!point_id) return;

    // 找到所属模块
    std::string pid(point_id);
    size_t dot = pid.find('.');
    if (dot == std::string::npos) return;

    std::string mod_name = pid.substr(0, dot);
    ForcePublishModule(mod_name.c_str());
}

bool DataPointRegistry::ForcePublishModule(const char* module_name) {
    if (!mqtt_connected_ || !mqtt_client_) return false;

    cJSON* data = cJSON_CreateObject();
    bool ok = SerializeModuleStatus(module_name, data);
    if (!ok) {
        cJSON_Delete(data);
        return false;
    }

    // 构建 schema 字符串
    char schema[64];
    snprintf(schema, sizeof(schema), "deep-trace.status.%s.v1", module_name);

    const std::string payload = deep_dog_envelope::Wrap(data, schema, mqtt_client_->settings().device_id);
    if (payload.empty()) {
        return false;
    }

    const std::string topic = deep_dog_topics::Status(module_name);
    bool result = mqtt_client_->Publish(topic, payload, 0, true);
    return result;
}

// ============================================================
// MQTT 对接
// ============================================================

void DataPointRegistry::SetMqttClient(DeepDogMqttClient* client) {
    mqtt_client_ = client;
}

void DataPointRegistry::OnMqttConnected() {
    mqtt_connected_ = true;

    if (!mqtt_client_) return;

    ESP_LOGI(TAG, "OnMqttConnected: publish %u module statuses + schema",
             (unsigned)impl_->modules.size());

    // 发布所有模块的 retain status
    for (const auto& entry : impl_->modules) {
        ForcePublishModule(entry.module_name.c_str());
    }

    // 发布 schema
    cJSON* schema_data = BuildSchemaJson();
    if (schema_data) {
        const std::string payload = deep_dog_envelope::Wrap(
            schema_data, deep_dog_envelope::SchemaInfo(), mqtt_client_->settings().device_id);
        if (!payload.empty()) {
            mqtt_client_->Publish(deep_dog_topics::Info("schema"), payload, 0, true);
        }
    }
}

void DataPointRegistry::OnMqttDisconnected() {
    mqtt_connected_ = false;
}

void DataPointRegistry::HandleCmdMessage(const std::string& module_name, const std::string& payload) {
    if (!mqtt_client_) return;

    auto it = impl_->module_index.find(module_name);
    if (it == impl_->module_index.end()) {
        // 多数模块不在数据点体系内（pairing/stream/face/track/config/motor…），静默跳过
        ESP_LOGD(TAG, "HandleCmdMessage: module '%s' not in registry, skip", module_name.c_str());
        return;
    }

    // 解析入站报文
    cJSON* data = deep_dog_envelope::ParseBusiness(payload);
    if (!data) {
        ESP_LOGW(TAG, "HandleCmdMessage: failed to parse payload for module '%s'", module_name.c_str());
        return;
    }

    const auto& entry = impl_->modules[it->second];
    int applied = 0;

    // 字段名 → 数据点解析：
    //  1. 单实例：`<point>`        → `module.<point>`
    //  2. 子实例：`<sub>_<point>`   → `module.<sub>.<point>`（见 interface/04 §3.7）
    const bool has_sub = entry.def.has_sub_instances;
    auto ResolveField = [&](const char* field) -> const DataPointDef* {
        if (has_sub) {
            const char* us = strchr(field, '_');
            if (us) {
                std::string pid = std::string(module_name) + "." + std::string(field, us - field) +
                                  "." + (us + 1);
                if (const DataPointDef* dp = FindPoint(pid.c_str())) return dp;
            }
        }
        std::string pid = std::string(module_name) + "." + field;
        return FindPoint(pid.c_str());
    };

    // 遍历 JSON 对象的每个字段，找到对应的数据点并写入
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, data) {
        const DataPointDef* dp = ResolveField(item->string);
        if (!dp) continue;

        if (dp->access == DP_ACCESS_RO) continue;

        DpValue val;
        bool valid = false;

        switch (dp->type) {
            case DP_TYPE_BOOL:
                if (cJSON_IsBool(item)) {
                    val.b = cJSON_IsTrue(item);
                    valid = true;
                }
                break;
            case DP_TYPE_INT:
                if (cJSON_IsNumber(item)) {
                    val.i = static_cast<int32_t>(item->valuedouble);
                    valid = true;
                }
                break;
            case DP_TYPE_FLOAT:
                if (cJSON_IsNumber(item)) {
                    val.f = static_cast<float>(item->valuedouble);
                    valid = true;
                }
                break;
            case DP_TYPE_STRING:
            case DP_TYPE_ENUM:
                if (cJSON_IsString(item)) {
                    val.s = item->valuestring;
                    valid = true;
                }
                break;
        }

        if (valid) {
            DpError err = ValidateAndWrite(dp, val);
            if (err == DP_ERR_OK) {
                ++applied;
            } else {
                ESP_LOGW(TAG, "HandleCmdMessage: write '%s' failed: err=%d", dp->id ? dp->id : "?",
                         err);
            }
        }
    }

    cJSON_Delete(data);

    if (applied > 0) {
        ForcePublishModule(module_name.c_str());
    }
}
