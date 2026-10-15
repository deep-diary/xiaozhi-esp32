# 04 · 数据点归一化（Data Point Registry）

> 契约版本：`interop-3` 候选（2026-10）——**声明式数据点目录 + 统一发布引擎**  
> 定位：本文件是 **interop 层架构需求**，约束设备端（deep-dog）与云平台端（deep-trace-com）之间  
> 数据点（可观测/可控制量）的统一描述、发现、发布与读写机制。  
> 关联：[01-mqtt-contract.md](./01-mqtt-contract.md) · [02-ws-contract.md](./02-ws-contract.md) · [03-http-api-contract.md](./03-http-api-contract.md)  
> 前置：interop-2（统一命名空间）已落地，本文在此基础上推进。

---

## 0. 背景与目标

### 0.1 问题陈述

当前设备端三通道（MQTT / WebSocket-MCP / HTTP-REST）的数据点定义**完全散落**，
存在以下结构性问题：

| # | 问题 | 影响 |
|---|------|------|
| P1 | 每个模块的 status 字段由 `cJSON_Add*ToObject` 动态拼装，无全局索引 | 无法自动发现、无法审计、文档与代码脱节 |
| P2 | 同一数据点在三通道重复定义（如电机角度在 MQTT status、MCP tool、HTTP API 三处各拼一次 JSON） | 字段名/单位/精度不一致风险高，维护成本三倍 |
| P3 | HTTP API 用 `snprintf` 手动拼接 JSON 字符串 | 易拼错、无类型检查、字段遗漏难发现 |
| P4 | 无统一类型/单位/范围系统，靠命名约定（`_rad`、`_nm`）兜底 | 前端无法自动渲染控件、自动校验输入 |
| P5 | 能力声明（`capabilities`）只有布尔开关，粒度粗 | 无法表达"有几个电机""每台电机支持哪些模式"等细粒度元信息 |
| P6 | telemetry 通道与各模块 status 双轨运行 | 数据重复、时序点和状态点可能不一致 |

### 0.2 目标

> **引入声明式数据点目录（Data Point Registry）+ 统一发布引擎，让每个数据点只声明一次，三通道自动生效。**

具体目标：

1. **单一真源**：每个数据点的类型、单位、范围、访问模式在一处声明
2. **三通道归一化**：MQTT status/cmd、HTTP REST、MCP tools 从同一声明生成
3. **自动发现**：设备上线时发布完整 schema，前端自动渲染模块卡和数值控件
4. **统一校验**：下行写入（cmd / HTTP POST / MCP set）统一做类型/范围校验
5. **架构可扩展**：新增数据点只需加一行声明，不用改三个通道的代码
6. **支撑前端设备页归一化**：schema 驱动的前端渲染，不同设备类型（产线 / ESP32 消费级）共用一套页面框架

### 0.3 非目标

- 不改变 MQTT topic 骨架（interop-2 的 `{prefix}/{device_id}/{kind}/{subject}` 保持不变）
- 不改变信封格式（`{schema, ts_iso, device_id, data}` 保持不变）
- 不替代复杂过程性 MCP 工具（如 `motor.scan`、`face.recognize`）——数据点是标量读写，复杂操作仍走独立 tool
- 不改变实时性要求——发布引擎只做序列化编排，不引入额外缓冲/延迟
- 不要求一次迁移所有模块——按模块增量迁移，新旧并行

---

## 1. 核心概念

### 1.1 数据点（Data Point）

> 一个**可观测或可控制的标量值**，具有唯一标识、明确类型、单位和范围。

数据点是本架构的原子单位。例如：

- `motor.0.position_rad`：0 号电机的位置（浮点，弧度，可读写）
- `motor.0.temperature`：0 号电机的温度（浮点，摄氏度，只读）
- `can.bitrate_kbps`：CAN 总线波特率（整型，kbps，只读）
- `led.brightness`：LED 亮度（整型，百分比，可读写）
- `system.free_heap`：空闲堆内存（整型，字节，只读）

### 1.2 模块（Module）

> 一组相关数据点的集合，与 MQTT subject 第一级对应。

模块 = M01 中的功能模块（device / system / motor / can / led / imu / servo / gimbal / ...）。

### 1.3 子实例（Sub-Instance）

> 同一模块下的多个同构实体，用 `sub_id` 区分。

例如：
- `motor` 模块有 N 个电机，`sub_id = 0..N-1`
- `led` 模块只有 1 个灯带，无 `sub_id`
- `can` 模块只有 1 路总线，无 `sub_id`

### 1.4 通道（Channel）

> 数据点的发布/访问通道。

| 通道 | 位标识 | 说明 |
|------|:------:|------|
| MQTT | `0b0001` | `status/<module>` retain 发布；`cmd/<module>` 接收写入 |
| HTTP | `0b0010` | `/api/datapoints/...` REST 访问 |
| MCP | `0b0100` | `self.<module>.get_<point>` / `set_<point>` 工具 |
| Telemetry | `0b1000` | 自动汇入 `status/telemetry` 的 `points[]` |

每个数据点通过位掩码选择生效通道（可组合）。

---

## 2. 数据点模型

### 2.1 完整 Schema

每个数据点的声明字段：

| 字段 | 类型 | 必填 | 说明 |
|------|------|:----:|------|
| `id` | string | ✅ | 全局唯一标识，格式 `module[.sub_id].point_name` |
| `name` | string | ✅ | 可读中文名（或英文），用于前端标签 |
| `module` | string | ✅ | 所属模块名，与 MQTT subject 第一级一致 |
| `sub_id` | string | ❌ | 子实例 ID（如电机号），无则为 null |
| `description` | string | ❌ | 详细描述 |
| `type` | enum | ✅ | 数据类型：`bool` / `int` / `float` / `string` / `enum` |
| `unit` | string | ❌ | 单位，空字符串表示无量纲 |
| `precision` | int | ❌ | 小数精度（float 型），默认 0 |
| `min` | number | ❌ | 下限，null 表示负无穷；写入时自动裁剪并告警 |
| `max` | number | ❌ | 上限，null 表示正无穷；写入时自动裁剪并告警 |
| `step` | number | ❌ | 步长 / 分辨率，默认 1（int）/ 最小精度（float） |
| `enum_values` | string[] | ❌ | 枚举值列表（type=enum 时必填） |
| `access` | enum | ✅ | 访问模式：`ro`（只读上报）/ `wo`（只写指令）/ `rw`（读写） |
| `channels` | bitmask | ✅ | 生效通道，见 §1.4 |
| `policy` | enum | ❌ | 发布策略：`retain` / `on_change` / `periodic` / `on_demand`，默认 `retain` |
| `period_ms` | int | ❌ | 周期发布间隔（policy=periodic 时），单位 ms |
| `deadband` | number | ❌ | 死区阈值（policy=on_change 时），变化超过此值才发布 |
| `read_cb` | callback | ✅ (ro/rw) | 读回调，返回当前值 |
| `write_cb` | callback | ✅ (wo/rw) | 写回调，接收新值并执行 |

### 2.2 类型系统

| 类型 | MQTT JSON 类型 | HTTP 响应类型 | MCP 参数类型 | 备注 |
|------|---------------|--------------|-------------|------|
| `bool` | boolean | boolean | boolean | |
| `int` | number (integer) | number (integer) | integer | 32 位有符号 |
| `float` | number | number | number | 精度由 `precision` 控制 |
| `string` | string | string | string | 最大长度 256 |
| `enum` | string | string | string (enum) | 取值来自 `enum_values` |

> **不支持**：object / array 等复合类型。复合数据（如电机数组）通过子实例机制展开，
> 或作为模块级 status 的整体结构（不属于数据点体系，走模块独立序列化）。

### 2.3 单位规范

数据点的 `unit` 字段为**权威单位声明**，字段名后缀（如 `_rad`、`_nm`）只是可读性辅助。

推荐单位：

| 量纲 | 推荐单位 | 示例 |
|------|---------|------|
| 角度 | `rad`（弧度）；前端显示可转 `°` | `position_rad` |
| 角速度 | `rad/s` | `speed_rad_s` |
| 力矩 | `N·m` | `torque_nm` |
| 温度 | `°C` | `temperature` |
| 时间 | `ms`（毫秒）；长周期用 `s` | `interval_ms` |
| 频率 | `Hz` | — |
| 波特率 | `kbps` | `bitrate_kbps` |
| 数据量 | `bytes` | `free_heap` |
| 百分比 | `%` | `brightness` |
| 电压 | `V` | — |
| 电流 | `A` | — |
| 加速度 | `g` | `accel_x_g` |

### 2.4 命名约定

```
<module>[.<sub_id>].<point_name>[_<unit_suffix>]
```

- `module`：小写，下划线分隔，如 `motor`、`face`、`system`
- `sub_id`：数字或简短标识，如 `0`、`left`
- `point_name`：小写，下划线分隔，描述语义
- `unit_suffix`：可选，仅当单位不为空且有助理解时加（如 `_rad`、`_ms`、`_kbps`）

---

## 3. 架构设计

### 3.1 总体架构

```
┌──────────────────────────────────────────────────────────────────┐
│                    Data Point Registry（全局单例）                │
│                                                                  │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐             │
│  │ Module:     │  │ Module:     │  │ Module:     │   ...       │
│  │  device     │  │  motor      │  │  system     │             │
│  │  - points[] │  │  - points[] │  │  - points[] │             │
│  │  - instances│  │  - instances│  │  - instances│             │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘             │
│         │                │                │                    │
│  ┌──────▼────────────────▼────────────────▼──────┐             │
│  │            Unified Publish Engine              │             │
│  │                                                │             │
│  │  ┌──────────┐ ┌──────────┐ ┌──────┐ ┌───────┐ │             │
│  │  │  MQTT    │ │  HTTP    │ │ MCP  │ │Telemet│ │             │
│  │  │Publisher │ │Publisher │ │Bridge│ │ Engine│ │             │
│  │  └──────────┘ └──────────┘ └──────┘ └───────┘ │             │
│  │                                                │             │
│  │  ┌──────────┐ ┌──────────┐                     │             │
│  │  │  Schema  │ │ Validator│                     │             │
│  │  │ Exporter │ │  Engine  │                     │             │
│  │  └──────────┘ └──────────┘                     │             │
│  └────────────────────────────────────────────────┘             │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  Hardware Abstraction（各模块硬件驱动 / 状态机）           │   │
│  │  （read_cb / write_cb 回调对接这一层，数据点不直接操作硬件）│   │
│  └──────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────┘
```

### 3.2 模块注册流程

```c
// 1. 定义数据点表（静态 const，存 Flash）
static const DataPointDef kMotorDataPoints[] = {
    {
        .id = "motor.position_rad",
        .name = "电机位置",
        .module = "motor",
        .type = DP_TYPE_FLOAT,
        .unit = "rad",
        .precision = 4,
        .min = -12.5f,
        .max = 12.5f,
        .access = DP_ACCESS_RW,
        .channels = DP_CH_MQTT | DP_CH_HTTP | DP_CH_MCP | DP_CH_TELEMETRY,
        .policy = DP_POLICY_RETAIN,
        .read_float = motor_read_position,
        .write_float = motor_write_position,
    },
    // ... 更多点
};

// 2. 注册模块（带子实例）
dp_register_module("motor", kMotorDataPoints, DP_ARRAY_SIZE(kMotorDataPoints),
                   DP_INSTANCE_MULTI, motor_get_instance_count, motor_get_instance_ids);
```

### 3.3 与 MQTT 的映射

| 数据点概念 | MQTT 映射 | 说明 |
|-----------|-----------|------|
| 模块 | `status/<module>` | 同一模块的所有数据点合并为一个 JSON 对象发布 |
| 子实例 | `data.motors[]` 数组中的对象 | 子实例数据点展开为数组元素的字段 |
| 只读点 | status JSON 字段 | retain 发布 |
| 读写点 | status 字段 + cmd 字段 | 上行发布当前值；下行 `cmd/<module>` 接收新值 |
| 发布策略 retain | MQTT retain = true | 连接即获取最新值 |
| 发布策略 on_change | MQTT retain = true | 仅在变化时更新 retain 值 |
| 发布策略 periodic | MQTT retain = false | 周期发布，不 retain |

**status payload 示例**（`status/motor`）：
```json
{
  "schema": "status.motor.v1",
  "ts_iso": "2026-10-15T08:30:00Z",
  "device_id": "1051db8118fc",
  "data": {
    "ok": true,
    "motors": [
      {
        "id": 1,
        "position_rad": 0.5236,
        "speed_rad_s": 0.0,
        "torque_nm": 0.12,
        "temperature": 32.5,
        "mode_status": "run",
        "fault": false
      }
    ]
  }
}
```

> 注意：非数据点字段（如 `ok`、`motors[].id`、`motors[].mcu_uid`）仍由模块自定义序列化，不进入数据点体系。
> 数据点体系覆盖的是**标量可观测/可控制量**。

### 3.4 与 HTTP API 的映射

新增统一端点：

| 端点 | 方法 | 说明 |
|------|------|------|
| `/api/datapoints/schema` | GET | 获取完整数据点 schema（JSON） |
| `/api/datapoints/module/<name>` | GET | 获取指定模块的所有点当前值 |
| `/api/datapoints/point/<module>.<id>` | GET | 获取单个数据点当前值 |
| `/api/datapoints/batch` | POST | 批量读取（body: `{"points":["motor.0.position_rad", ...]}`） |
| `/api/datapoints/set` | POST | 写入单个点（body: `{"id":"led.brightness", "value":80}`） |
| `/api/datapoints/batch_set` | POST | 批量写入 |

**响应格式**（统一）：
```json
{
  "ok": true,
  "data": {
    "motor.0.position_rad": {"value": 0.5236, "ts": 1728981000},
    "motor.0.temperature": {"value": 32.5, "ts": 1728981000}
  },
  "error": null
}
```

**写入错误响应**：
```json
{
  "ok": false,
  "error": {
    "code": "OUT_OF_RANGE",
    "message": "Value 15.0 exceeds max 12.5 for motor.0.position_rad",
    "min": -12.5,
    "max": 12.5
  }
}
```

> 旧 HTTP 端点（`/api/status`、`/api/dog_status` 等）作为兼容层保留，内部转发到数据点引擎。
> 兼容层在所有前端迁移完成后下线。

### 3.5 与 MCP 的映射

数据点自动生成 MCP 工具：

| 数据点属性 | MCP 工具映射 |
|-----------|-------------|
| 模块 | tool 前缀 `self.<module>.` |
| 只读点 | `get_<point_name>` 工具，无参，返回值 |
| 读写点 | `get_<point_name>` + `set_<point_name>` 两个工具 |
| 只写点 | `set_<point_name>` 工具 |
| 子实例 | tool name 包含 sub_id，如 `self.motor.get_0_position_rad` |
| 类型/范围 | 自动填入 MCP tool 的 `inputSchema` |

**示例**：
```json
// MCP tools/list 输出中的一个工具
{
  "name": "self.motor.set_0_position_rad",
  "description": "设置0号电机位置（弧度）",
  "inputSchema": {
    "type": "object",
    "properties": {
      "value": {
        "type": "number",
        "minimum": -12.5,
        "maximum": 12.5,
        "description": "目标位置，单位 rad"
      }
    },
    "required": ["value"]
  }
}
```

> 复杂过程性操作（如 `motor.scan`、`dog.init`、`face.recognize`）**不进入**数据点体系，
> 仍由各模块独立注册 MCP 工具。数据点只覆盖「标量读写」。

### 3.6 与 Telemetry 的映射

标记了 `DP_CH_TELEMETRY` 的数值型数据点（int / float），由发布引擎自动汇入
`status/telemetry` 主题的 `points[]` 数组。

**telemetry payload 示例**：
```json
{
  "schema": "status.telemetry.v1",
  "ts_iso": "2026-10-15T08:30:00Z",
  "device_id": "1051db8118fc",
  "data": {
    "seq": 42,
    "points": [
      {"key": "motor.0.position_rad", "ts": 1728981000123, "value": 0.5236},
      {"key": "motor.0.speed_rad_s", "ts": 1728981000123, "value": 0.0},
      {"key": "motor.0.temperature", "ts": 1728981000123, "value": 32.5},
      {"key": "system.free_heap", "ts": 1728981000123, "value": 152000}
    ]
  }
}
```

每个点自带单位等元信息——这些元信息通过 `info/schema` 发布，不重复出现在每个 telemetry 包中。

---

## 4. Schema 自动发现

### 4.1 `info/schema` 主题

新增 MQTT 主题 `info/schema`（retain），设备上线时发布完整数据点目录。

**Payload 结构**：
```json
{
  "schema": "info.schema.v1",
  "ts_iso": "2026-10-15T08:30:00Z",
  "device_id": "1051db8118fc",
  "data": {
    "schema_version": "1.0.0",
    "modules": {
      "device": {
        "name": "设备核心",
        "description": "所有设备共有的核心身份与状态信息",
        "instances": null,
        "points": {
          "device_id": {
            "type": "string",
            "access": "ro",
            "channels": ["mqtt", "http"],
            "description": "设备唯一ID（MAC紧凑串）"
          },
          "firmware": {
            "type": "string",
            "access": "ro",
            "channels": ["mqtt", "http"],
            "description": "固件版本"
          },
          "online": {
            "type": "bool",
            "access": "ro",
            "channels": ["mqtt", "http"],
            "description": "设备是否在线"
          }
        }
      },
      "system": {
        "name": "系统资源",
        "description": "MCU 系统资源与诊断信息",
        "instances": null,
        "points": {
          "free_heap": {
            "type": "int",
            "unit": "bytes",
            "access": "ro",
            "channels": ["mqtt", "http", "mcp", "telemetry"],
            "min": 0,
            "description": "空闲堆内存大小"
          },
          "free_psram": {
            "type": "int",
            "unit": "bytes",
            "access": "ro",
            "channels": ["mqtt", "http", "telemetry"],
            "min": 0,
            "description": "空闲 PSRAM 大小"
          },
          "cpu_load": {
            "type": "float",
            "unit": "%",
            "access": "ro",
            "channels": ["mqtt", "http", "telemetry"],
            "min": 0,
            "max": 100,
            "precision": 1,
            "description": "CPU 使用率"
          }
        }
      },
      "motor": {
        "name": "电机控制",
        "description": "CAN 总线电机控制与状态",
        "instances": {
          "type": "numeric",
          "count": 12,
          "id_range": [1, 12]
        },
        "points": {
          "position_rad": {
            "type": "float",
            "unit": "rad",
            "access": "rw",
            "channels": ["mqtt", "http", "mcp", "telemetry"],
            "min": -12.5,
            "max": 12.5,
            "precision": 4,
            "description": "电机位置（弧度）"
          },
          "speed_rad_s": {
            "type": "float",
            "unit": "rad/s",
            "access": "ro",
            "channels": ["mqtt", "telemetry"],
            "precision": 3,
            "description": "电机转速（弧度/秒）"
          },
          "temperature": {
            "type": "float",
            "unit": "°C",
            "access": "ro",
            "channels": ["mqtt", "http", "telemetry"],
            "min": -40,
            "max": 125,
            "precision": 1,
            "description": "电机温度"
          }
        }
      }
    }
  }
}
```

### 4.2 前端使用方式

前端设备页加载流程：

1. 连接 MQTT → 订阅 `info/schema`（retain，立即拿到）
2. 解析 schema → 按 `modules` 生成模块卡片列表
3. 每个模块的 points → 生成数据点展示/控件
4. 根据 `channels` 决定：订阅 MQTT status / 轮询 HTTP / 显示 MCP 按钮
5. 带 `telemetry` 标志的点 → 自动出现在信号追溯的信号树中

---

## 5. 校验引擎

### 5.1 写入校验流程

所有通道的写入请求（MQTT cmd / HTTP POST / MCP set）都经过统一校验引擎：

```
写入请求（模块, 点ID, 值）
    │
    ▼
查找数据点定义 ──→ 未找到 → 返回 UNKNOWN_POINT 错误
    │
    ▼
检查访问权限 ──→ 只读点 → 返回 READ_ONLY 错误
    │
    ▼
类型校验 ──→ 类型不匹配 → 返回 TYPE_MISMATCH 错误
    │
    ▼
枚举校验 (enum) ──→ 不在枚举中 → 返回 INVALID_ENUM 错误
    │
    ▼
范围校验 (数值) ──→ 超出范围 → 返回 OUT_OF_RANGE 错误
    │         └────── 超出但可裁剪 → 裁剪后继续（可选策略）
    ▼
调用 write_cb ──→ 失败 → 返回 WRITE_FAILED 错误
    │
    ▼
成功 → 触发发布（retain 更新 + on_change 检测）
```

### 5.2 错误码

| 错误码 | HTTP 状态 | 说明 |
|--------|-----------|------|
| `UNKNOWN_POINT` | 404 | 数据点不存在 |
| `READ_ONLY` | 403 | 数据点只读，不可写入 |
| `WRITE_ONLY` | 403 | 数据点只写，不可读取 |
| `TYPE_MISMATCH` | 400 | 值类型与数据点类型不匹配 |
| `INVALID_ENUM` | 400 | 枚举值不在允许列表中 |
| `OUT_OF_RANGE` | 400 | 数值超出 min/max 范围 |
| `WRITE_FAILED` | 500 | 硬件写入失败 |
| `NOT_SUPPORTED` | 501 | 当前硬件/编译配置不支持此数据点 |

---

## 6. 实施路线

### 6.1 阶段划分

| 阶段 | 内容 | 目标模块 | 交付物 |
|------|------|---------|--------|
| **Phase 0** | 框架搭建：Registry 数据结构 + 发布引擎核心 + Schema Exporter | — | 基础库代码 |
| **Phase 1** | 迁简单模块验证：`led`（点数少，读写清晰） | led | led 模块从手工 cJSON 改声明式 |
| **Phase 2** | 迁核心模块：`device`（核心身份点） + `system`（系统资源点） | device, system | info/schema 可用；SRAM/PSRAM/任务栈归为 system 模块 |
| **Phase 3** | 迁中等模块：`servo`、`gimbal`、`touch`、`handle` | 4 个模块 | 验证子实例/数组机制 |
| **Phase 4** | 迁复杂模块：`motor`、`can`、`imu` | motor, can, imu | 验证大量子实例 + 高频发布 |
| **Phase 5** | HTTP 统一端点 + 旧端点兼容层 | — | `/api/datapoints/*` 可用 |
| **Phase 6** | MCP 自动生成 RW 工具 | 已迁移的模块 | 消除重复 MCP 注册代码 |
| **Phase 7** | Telemetry 自动归并 | 带 telemetry 标志的点 | 消除数据双轨 |

### 6.2 增量迁移策略

- **新旧并行**：未迁移的模块继续用原有 cJSON 方式，不影响功能
- **按模块粒度切换**：每个模块独立迁移、独立验证
- **向后兼容**：topic 路径、字段名、单位均不变，只改序列化方式
- **可回滚**：迁移失败可快速切回旧代码

---

## 7. 与前端设备页归一化的关系

数据点归一化是前端设备页归一化的**数据层基础**：

| 前端归一化需求 | 数据点归一化支撑 |
|---------------|-----------------|
| 模块卡片按 capabilities 显隐 | `info/schema.modules` 是 capabilities 的细粒度超集 |
| 设备信息只显示共有属性 | `module=device` 的点 = 所有设备共有核心 |
| SRAM/PSRAM/任务栈封装成模块 | 归为 `module=system` 数据点集 |
| 外接接口也是功能模块 | CAN/UART/PWM 都是独立 module，schema 中并列 |
| 产线专用模块（OEE/质量/维护） | 产线设备的 schema 中有对应 module，消费级设备没有 |
| 子单元列表 | 数据点的 `instances` + `sub_id` 机制 = 子单元结构 |
| 前端自动渲染数值控件 | schema 中的 type/min/max/unit → 自动生成滑块/输入框/单位标签 |
| 信号追溯数据源 | telemetry 标记的数据点 → 自动出现在信号树中 |
| 不同设备类型归一化 | 不同设备类型 = 不同的 schema 内容，前端框架同一套 |

---

## 8. 验收标准

### 8.1 功能验收

- [ ] `info/schema` 主题可正常发布，内容包含所有已迁移模块的数据点定义
- [ ] MQTT `status/<module>` payload 与迁移前**完全一致**（字段名、类型、值不变）
- [ ] HTTP `/api/datapoints/schema` 返回完整 schema
- [ ] HTTP `/api/datapoints/module/<name>` 返回模块所有点当前值
- [ ] HTTP 写入经过统一校验，超出范围返回正确错误码
- [ ] MCP `tools/list` 中自动出现 `get_` / `set_` 工具，inputSchema 正确
- [ ] telemetry `points[]` 中包含所有带 telemetry 标志的数值点
- [ ] 新增一个数据点只需修改声明表，无需改动三通道代码

### 8.2 性能验收

- [ ] status 发布延迟（从值变化到 MQTT publish）与迁移前相比增加 ≤ 5%
- [ ] 内存占用增加 ≤ 10KB（Flash 中的静态表不计入 RAM）
- [ ] `info/schema` payload 大小 ≤ 8KB（中等模块数设备）

### 8.3 可维护性验收

- [ ] 全模块迁移后，`cJSON_Add*ToObject` 调用次数减少 ≥ 60%
- [ ] HTTP handler 数量减少 ≥ 50%（统一端点替代多个专用端点）
- [ ] MCP 工具注册代码减少 ≥ 40%（RW 点自动生成）

---

## 9. 风险与缓解

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|:----:|---------|
| 框架抽象层引入性能开销 | 实时性下降 | 中 | Phase 1 完成后做基准测试；热点路径可旁路 |
| 复杂模块（motor/can）迁移困难 | 进度延迟 | 中 | 先迁核心标量点，非数据点字段保留旧序列化；增量推进 |
| 旧 HTTP 端点兼容层出 bug | 前端页面异常 | 低 | 双写双读验证；保留快速回滚开关 |
| Schema 膨胀（设备类型多、模块多） | 前端解析慢 | 低 | 分页/按需加载 schema；schema 做 gzip 压缩（HTTP 侧） |
| 与现有 telemetry 机制冲突 | 数据重复 | 低 | Phase 7 前保持双轨；验证一致后再下线旧路径 |

---

## 10. 相关文档

- [01-mqtt-contract.md](./01-mqtt-contract.md) — MQTT 标准接口契约（topic 骨架 + 信封）
- [02-ws-contract.md](./02-ws-contract.md) — WebSocket 标准接口契约（MCP 通道）
- [03-http-api-contract.md](./03-http-api-contract.md) — HTTP REST 标准接口契约
- [00-architecture-review.md](./00-architecture-review.md) — 现状架构审查
- [swrs/mqtt/M01-board-mqtt-protocol.md](../../swrs/mqtt/M01-board-mqtt-protocol.md) — M01 板级 MQTT 总览
- [swrs/mqtt/protocol/deep-dog-mqtt.yml](../../swrs/mqtt/protocol/deep-dog-mqtt.yml) — MQTT YAML 契约真源
