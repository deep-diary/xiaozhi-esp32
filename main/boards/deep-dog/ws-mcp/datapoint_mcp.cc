#include "ws-mcp/datapoint_mcp.h"

#include "mqtt/datapoint_registry.h"
#include "mcp_server.h"

#include <esp_log.h>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cmath>

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
 * 对于形如 "led.brightness" 的点 id，module="led"，name="brightness"。
 * 含 sub_id 的如 "motor.0.speed" → module="motor"，name="0_speed"。
 */
std::string MakeToolName(const DataPointDef* point, const char* prefix) {
    std::string id = point->id;
    // id 格式: <module>[.<sub_id>].<point_name>
    // module 是第一段，剩余部分用下划线拼接
    size_t first_dot = id.find('.');
    std::string module = (first_dot == std::string::npos) ? id : id.substr(0, first_dot);
    std::string rest = (first_dot == std::string::npos) ? id : id.substr(first_dot + 1);
    // 把剩余的 '.' 替换成 '_'
    for (auto& c : rest) {
        if (c == '.') c = '_';
    }
    return std::string("self.") + module + "." + prefix + "_" + rest;
}

std::string MakeGetDescription(const DataPointDef* point) {
    std::string desc = "Get current value of data point ";
    desc += point->id;
    if (point->name && point->name[0]) {
        desc += " (";
        desc += point->name;
        desc += ")";
    }
    if (point->unit && point->unit[0]) {
        desc += " [unit: ";
        desc += point->unit;
        desc += "]";
    }
    return desc;
}

std::string MakeSetDescription(const DataPointDef* point) {
    std::string desc = "Set value of data point ";
    desc += point->id;
    if (point->name && point->name[0]) {
        desc += " (";
        desc += point->name;
        desc += ")";
    }
    if (point->unit && point->unit[0]) {
        desc += " [unit: ";
        desc += point->unit;
        desc += "]";
    }
    return desc;
}

/**
 * 读取数据点值，构造 MCP ReturnValue。
 */
ReturnValue ReadPointValue(const DataPointDef* point) {
    if (!point) return std::string("error: null point");

    switch (point->type) {
        case DP_TYPE_BOOL: {
            bool val = false;
            if (point->read_bool) {
                val = point->read_bool(point->read_ctx);
            }
            return val;
        }
        case DP_TYPE_INT: {
            int val = 0;
            if (point->read_int) {
                val = point->read_int(point->read_ctx);
            }
            return val;
        }
        case DP_TYPE_ENUM: {
            // 枚举以字符串形式返回（语义为字符串）
            std::string val;
            if (point->read_string) {
                const char* s = point->read_string(point->read_ctx);
                if (s) val = s;
            }
            return val;
        }
        case DP_TYPE_FLOAT: {
            // float 以字符串返回，避免精度丢失
            float val = 0.0f;
            if (point->read_float) {
                val = point->read_float(point->read_ctx);
            }
            char buf[32];
            snprintf(buf, sizeof(buf), "%.*f", point->precision, (double)val);
            return std::string(buf);
        }
        case DP_TYPE_STRING: {
            std::string val;
            if (point->read_string) {
                const char* s = point->read_string(point->read_ctx);
                if (s) val = s;
            }
            return val;
        }
        default:
            return std::string("error: unsupported type");
    }
}

/**
 * 从 MCP PropertyList 中取 "value" 参数，写入数据点。
 * 返回结果字符串（"ok" 或 "error: ..."）。
 */
std::string WritePointFromProps(const DataPointDef* point, const PropertyList& props) {
    if (!point) return "error: null point";

    DpValue dpv{};

    switch (point->type) {
        case DP_TYPE_BOOL: {
            dpv.b = props["value"].value<bool>();
            break;
        }
        case DP_TYPE_INT: {
            dpv.i = props["value"].value<int>();
            break;
        }
        case DP_TYPE_ENUM: {
            std::string s = props["value"].value<std::string>();
            dpv.s = s.c_str();
            break;
        }
        case DP_TYPE_FLOAT: {
            // float 以字符串形式传入
            std::string s = props["value"].value<std::string>();
            dpv.f = strtof(s.c_str(), nullptr);
            break;
        }
        case DP_TYPE_STRING: {
            std::string s = props["value"].value<std::string>();
            dpv.s = s.c_str();
            break;
        }
        default:
            return "error: unsupported type";
    }

    auto& reg = DataPointRegistry::Instance();
    DpError err = reg.WriteValue(point->id, dpv);
    if (err != DP_ERR_OK) {
        char buf[64];
        snprintf(buf, sizeof(buf), "error: code=%d", (int)err);
        return std::string(buf);
    }
    return "ok";
}

/**
 * 为 set_<point> 工具构造 input schema（单个 "value" 参数）。
 */
PropertyList BuildSetInputProps(const DataPointDef* point) {
    PropertyType ptype = MapDpTypeToMcpProp(point->type);

    switch (point->type) {
        case DP_TYPE_INT: {
            int mn = static_cast<int>(point->min_val);
            int mx = static_cast<int>(point->max_val);
            // 只有 min < max 且非默认极值时才加范围约束
            if (mn < mx && mn > -1000000 && mx < 1000000) {
                return PropertyList({Property("value", ptype, mn, mx)});
            }
            return PropertyList({Property("value", ptype)});
        }
        default:
            return PropertyList({Property("value", ptype)});
    }
}

}  // namespace

int RegisterDataPointMcpTools(McpServer& mcp_server) {
    auto& reg = DataPointRegistry::Instance();
    int count = 0;

    size_t mod_count = reg.ModuleCount();
    for (size_t mi = 0; mi < mod_count; ++mi) {
        const DataModuleDef* module = reg.GetModule(mi);
        if (!module) continue;

        for (size_t i = 0; i < module->point_count; ++i) {
            const DataPointDef* p = &module->points[i];
            if (!(p->channels & DP_CH_MCP)) continue;

            // ---- get 工具（可读都有） ----
            if (p->access != DP_ACCESS_WO) {
                std::string tool_name = MakeToolName(p, "get");
                std::string tool_desc = MakeGetDescription(p);
                const DataPointDef* captured = p;
                mcp_server.AddTool(
                    tool_name, tool_desc, PropertyList(),
                    [captured](const PropertyList&) -> ReturnValue {
                        return ReadPointValue(captured);
                    });
                ++count;
            }

            // ---- set 工具（可写都有） ----
            if (p->access != DP_ACCESS_RO) {
                std::string tool_name = MakeToolName(p, "set");
                std::string tool_desc = MakeSetDescription(p);
                PropertyList input = BuildSetInputProps(p);
                const DataPointDef* captured = p;
                mcp_server.AddTool(
                    tool_name, tool_desc, input,
                    [captured](const PropertyList& props) -> ReturnValue {
                        return WritePointFromProps(captured, props);
                    });
                ++count;
            }
        }
    }

    ESP_LOGI(TAG, "registered %d auto MCP tools from data point registry", count);
    return count;
}
