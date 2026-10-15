# deep-dog 板级文档

`main/boards/deep-dog/` 的**文档唯一汇总根**。所有板级文档按「场景」落到本文三大域之一；写作规则与模板见 [CONVENTIONS.md](./CONVENTIONS.md)。

## 目录结构

```
main/boards/deep-dog/docs/
├── README.md            # 本文件：文档总枢纽
├── CONVENTIONS.md       # 写作规范：场景 → 目录 → 命名 → 格式
├── requirements/        # 板内需求 / 路线图 / MQTT 契约 / 架构图
│   ├── README.md  ROADMAP.md  AUDIT_REPORT.md
│   ├── architecture/    # 架构图集（HTML + Mermaid）
│   ├── mqtt/  vision/  motor/  input/  net/  config/  dog/  ws-mcp/
├── interface/           # 设备 ⇄ 云平台 跨端接口契约（MQTT/WS/HTTP）
└── ll/                  # Lessons Learned 经验教训
```

## 场景路由表（速查）

| 场景 | 落到 | 详见 |
|------|------|------|
| 经验教训 / bug 排查结论 | `ll/` | [CONVENTIONS §3.1](./CONVENTIONS.md) |
| 新需求 / 功能变更（需求先行） | `requirements/<域>/` | [CONVENTIONS §3.2](./CONVENTIONS.md) |
| 跨端接口（MQTT/WS/HTTP）契约 | `interface/` | [CONVENTIONS §3.3](./CONVENTIONS.md) |
| MQTT 模块前端契约 | `requirements/mqtt/modules/` | [CONVENTIONS §3.4](./CONVENTIONS.md) |
| 架构总览 / 机理解析 | `requirements/architecture/` | [CONVENTIONS §3.5](./CONVENTIONS.md) |
| 交付顺序 / 状态 | `requirements/ROADMAP.md` | [ROADMAP.md](./requirements/ROADMAP.md) |

## 三大域索引

### requirements —— 板内需求与契约

- 索引：[requirements/README.md](./requirements/README.md)
- 交付顺序：[requirements/ROADMAP.md](./requirements/ROADMAP.md)
- 架构图集：[requirements/architecture/index.html](./requirements/architecture/index.html)
- MQTT 契约：[requirements/mqtt/README.md](./requirements/mqtt/README.md)（字段真源 `mqtt/protocol/deep-dog-mqtt.yml`）
- 子域：`mqtt/` · `vision/` · `motor/` · `input/` · `net/` · `config/` · `dog/` · `ws-mcp/`

### interface —— 跨端接口契约

设备端（xiaozhi-esp32 / deep-dog）与云平台端（deep-trace-com）之间的接口基线，两端独立开发、并行联调。

- 索引：[interface/README.md](./interface/README.md)
- 契约：MQTT · WebSocket · HTTP/REST · Data Point Registry

### ll —— 经验教训

开发 / 联调中踩过、结论可复用的坑。

- 索引：[ll/README.md](./ll/README.md)

## 约定

- **单一真源**：同一事实只有一处权威（如 MQTT 字段真源 = `requirements/mqtt/protocol/deep-dog-mqtt.yml`）。
- **需求先行**：新功能先补/更新需求文档，再改实现代码（见 [CONVENTIONS.md](./CONVENTIONS.md)）。
- **新增即挂索引**：新增文档必须挂到所属域 README，必要时同步 `ROADMAP.md`。
- **跨仓引用用绝对路径**；禁止写入密码 / Token / 密钥。
