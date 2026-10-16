#include "ws-mcp/datapoint_mcp.h"

#include "mqtt/datapoint_registry.h"
#include "mcp_server.h"

#include <cJSON.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <esp_log.h>

#define TAG "dp_mcp"

namespace {

/**
 * 把数据点类型映射到 MCP PropertyType。
 * 注意：MCP Property 只有 bool/int/string 三种基本类型，
 * float 数据点以 string 形式传递（保留精度灵活性）。
 */
PropertyType MapDpTypeToMcpProp(DpType type) {
    switch (type) {
        case DP_TYPE_BOOL:   return kPropertyTypeBoolean;
        case DP_TYPE_INT:    return kPropertyTypeInteger;
        case DP_TYPE_ENUM:   return kPropertyTypeInteger;
        case DP_TYPE_FLOAT:  return kPropertyTypeString;  // float 以字符串传递
        case DP_TYPE_STRING: return kPropertyTypeString;
        default:             return kPropertyTypeString;
    }
}

/**
 * 生成 self.<module>.<get|set>_<point> 工具名。
 * 对于形如 "led.brightness" 的点 id，module="led"，name="brightness"；
 * 含 sub_id 的如 "motor.0.speed" → module="motor"，name="0_speed"。
 */
void MakeToolName(const DataPointDef* point, bool is_set, char* out, size_t cap) {
    const char* id = point->id;
    const char* first_dot = strchr(id, '.');
    if (!first_dot) {
        snprintf(out, cap, "self.%s.%s_%s", id, is_set ? "set" : "get", id);
        return;
    }
    const size_t module_len = static_cast<size_t>(first_dot - id);
    char rest[80];
    size_t r = 0;
    for (const char* p = first_dot + 1; *p && r + 1 < sizeof(rest); ++p) {
        rest[r++] = (*p == '.') ? '_' : *p;
    }
    rest[r] = '\0';
    snprintf(out, cap, "self.%.*s.%s_%s", static_cast<int>(module_len), id,
             is_set ? "set" : "get", rest);
}

void MakeDescription(const DataPointDef* point, bool is_set, char* out, size_t cap) {
    char unit[32] = "";
    if (point->unit && point->unit[0]) {
        snprintf(unit, sizeof(unit), " [unit: %s]", point->unit);
    }
    snprintf(out, cap, "%s value of data point %s%s", is_set ? "Set" : "Get", point->id, unit);
}

/** set_<point> 工具入参 schema（单个 "value" 参数；int 带范围约束）。 */
PropertyList BuildSetInputProps(const DataPointDef* point) {
    PropertyType ptype = MapDpTypeToMcpProp(point->type);
    if (point->type == DP_TYPE_INT) {
        const int mn = static_cast<int>(point->min_val);
        const int mx = static_cast<int>(point->max_val);
        // 只有 min < max 且非默认极值时才加范围约束
        if (mn < mx && mn > -1000000 && mx < 1000000) {
            return PropertyList({Property("value", ptype, mn, mx)});
        }
    }
    return PropertyList({Property("value", ptype)});
}

/**
 * 遍历「自动工具」序列（模块顺序 × 点位顺序，每个点位先 get 后 set）。
 * 回调返回 false 即提前结束。
 */
template <typename Fn>
void ForEachAutoTool(Fn&& fn) {
    auto& reg = DataPointRegistry::Instance();
    const size_t mod_count = reg.ModuleCount();
    size_t idx = 0;
    for (size_t mi = 0; mi < mod_count; ++mi) {
        const DataModuleDef* module = reg.GetModule(mi);
        if (!module) {
            continue;
        }
        for (size_t i = 0; i < module->point_count; ++i) {
            const DataPointDef* p = &module->points[i];
            if (!(p->channels & DP_CH_MCP)) {
                continue;
            }
            // get（可读都有）
            if (p->access != DP_ACCESS_WO) {
                if (!fn(idx, p, false)) {
                    return;
                }
                ++idx;
            }
            // set（可写都有）
            if (p->access != DP_ACCESS_RO) {
                if (!fn(idx, p, true)) {
                    return;
                }
                ++idx;
            }
        }
    }
}

/** 读取数据点值 → ReturnValue。 */
ReturnValue ReadPointValue(const DataPointDef* point) {
    if (!point) {
        return std::string("error: null point");
    }
    switch (point->type) {
        case DP_TYPE_BOOL:
            return point->read_bool ? point->read_bool(point->read_ctx) : false;
        case DP_TYPE_INT:
            return point->read_int ? point->read_int(point->read_ctx) : 0;
        case DP_TYPE_ENUM:
        case DP_TYPE_STRING: {
            std::string val;
            if (point->read_string) {
                const char* s = point->read_string(point->read_ctx);
                if (s) {
                    val = s;
                }
            }
            return val;
        }
        case DP_TYPE_FLOAT: {
            float val = point->read_float ? point->read_float(point->read_ctx) : 0.0f;
            char buf[32];
            snprintf(buf, sizeof(buf), "%.*f", point->precision, static_cast<double>(val));
            return std::string(buf);
        }
        default:
            return std::string("error: unsupported type");
    }
}

/** 从 MCP 入参写数据点，返回 "ok" / "error: ..."。 */
std::string WritePointFromProps(const DataPointDef* point, const PropertyList& props) {
    if (!point) {
        return "error: null point";
    }
    DpValue dpv{};
    std::string value_str;
    switch (point->type) {
        case DP_TYPE_BOOL:
            dpv.b = props["value"].value<bool>();
            break;
        case DP_TYPE_INT:
            dpv.i = props["value"].value<int>();
            break;
        case DP_TYPE_ENUM:
            value_str = props["value"].value<std::string>();
            dpv.s = value_str.c_str();
            break;
        case DP_TYPE_FLOAT:
            value_str = props["value"].value<std::string>();
            dpv.f = strtof(value_str.c_str(), nullptr);
            break;
        case DP_TYPE_STRING:
            value_str = props["value"].value<std::string>();
            dpv.s = value_str.c_str();
            break;
        default:
            return "error: unsupported type";
    }

    const DpError err = DataPointRegistry::Instance().WriteValue(point->id, dpv);
    if (err != DP_ERR_OK) {
        char buf[64];
        snprintf(buf, sizeof(buf), "error: code=%d", static_cast<int>(err));
        return std::string(buf);
    }
    return "ok";
}

/**
 * 数据点自动工具源：**零 per-tool 堆对象**（F-12）。
 * 单例为静态存储；名字/描述/schema 在每次调用时现算到调用方栈缓冲。
 */
class DataPointAutoTools : public AutoToolSource {
public:
    size_t ToolCount() const override {
        size_t count = 0;
        ForEachAutoTool([&count](size_t, const DataPointDef*, bool) {
            ++count;
            return true;
        });
        return count;
    }

    bool DescribeTool(size_t i, char* name, size_t name_cap, char* desc, size_t desc_cap,
                      PropertyList* props, bool* user_only) const override {
        bool found = false;
        ForEachAutoTool([&](size_t idx, const DataPointDef* p, bool is_set) {
            if (idx != i) {
                return true;
            }
            MakeToolName(p, is_set, name, name_cap);
            MakeDescription(p, is_set, desc, desc_cap);
            if (props) {
                *props = is_set ? BuildSetInputProps(p) : PropertyList();
            }
            if (user_only) {
                // 仅本地「全量」通道可见：云端 tools/list 只承载语义化手写工具
                // （docs/interface/05-mcp-tool-normalization.md）
                *user_only = true;
            }
            found = true;
            return false;
        });
        return found;
    }

    bool InvokeTool(const char* name, const PropertyList& props, std::string* result) const override {
        if (!name) {
            return false;
        }
        const DataPointDef* hit = nullptr;
        bool hit_is_set = false;
        char buf[96];
        ForEachAutoTool([&](size_t, const DataPointDef* p, bool is_set) {
            MakeToolName(p, is_set, buf, sizeof(buf));
            if (strcmp(buf, name) == 0) {
                hit = p;
                hit_is_set = is_set;
                return false;
            }
            return true;
        });
        if (!hit) {
            return false;
        }
        if (result) {
            if (hit_is_set) {
                *result = WritePointFromProps(hit, props);
            } else {
                // get：与 McpTool::Call 的返回形态保持一致（content[0].text + isError）
                const ReturnValue rv = ReadPointValue(hit);
                cJSON* root = cJSON_CreateObject();
                cJSON* content = cJSON_CreateArray();
                cJSON* text = cJSON_CreateObject();
                cJSON_AddStringToObject(text, "type", "text");
                if (std::holds_alternative<bool>(rv)) {
                    cJSON_AddStringToObject(text, "text", std::get<bool>(rv) ? "true" : "false");
                } else if (std::holds_alternative<int>(rv)) {
                    cJSON_AddStringToObject(text, "text", std::to_string(std::get<int>(rv)).c_str());
                } else if (std::holds_alternative<std::string>(rv)) {
                    cJSON_AddStringToObject(text, "text", std::get<std::string>(rv).c_str());
                } else {
                    cJSON_AddStringToObject(text, "text", "ok");
                }
                cJSON_AddItemToArray(content, text);
                cJSON_AddItemToObject(root, "content", content);
                cJSON_AddBoolToObject(root, "isError", false);
                char* json_str = cJSON_PrintUnformatted(root);
                *result = json_str ? json_str : "{}";
                if (json_str) {
                    cJSON_free(json_str);
                }
                cJSON_Delete(root);
            }
        }
        return true;
    }
};

DataPointAutoTools g_dp_auto_tools;

}  // namespace

int RegisterDataPointMcpTools(McpServer& mcp_server) {
    mcp_server.SetAutoToolSource(&g_dp_auto_tools);
    // 注：此处仅挂接「源」；工具数量在 tools/list / tools/call 时按注册表现算，
    // 故不在挂接时刻打数量（那时模块点位可能尚未注册完毕）。
    ESP_LOGI(TAG, "data point auto tools attached (on-demand, no per-tool heap; tools=%u now)",
             static_cast<unsigned>(g_dp_auto_tools.ToolCount()));
    return static_cast<int>(g_dp_auto_tools.ToolCount());
}
