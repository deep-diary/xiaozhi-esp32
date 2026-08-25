# CloudEdge（micro-ROS）

> A3 CloudEdge 薄边缘线在 deep-dog 上的固件需求入口。  
> **权威产品契约在 a3_arm_ws**（只读参考）；本目录维护固件侧可追溯切片与实现指针。

## 契约仓路径（只读）

| 场景 | 路径 |
|------|------|
| WSL | `/home/Blue/dev/a3_arm_ws` |
| Windows UNC | `\\wsl.localhost\Ubuntu-22.04\home\Blue\dev\a3_arm_ws` |

优先阅读：

- `docs/cloud_edge/REQUIREMENTS.md`（阶段 A / E1、E9、E4）
- `docs/shared/TOPIC_CONTRACT.md`
- `docs/cloud_edge/QUICKSTART.md`
- `AGENT.md`

**禁止**把 `a3_arm_ws` 源码树拷进本仓；禁止擅自改话题名 / 关节名 / 消息类型。

## 本目录文档

| ID | 文档 | 说明 |
|----|------|------|
| **CE01** | [CE01-microros-link-smoke.md](./CE01-microros-link-smoke.md) | 阶段 A：WiFi + Agent + 订轨迹 + 50 Hz `joint_states`（可 mock） |

## 代码

| 路径 | 说明 |
|------|------|
| [`microros/`](../../microros/) | Client 实现与联调 README |
| [`board_features.h`](../../board_features.h) | `DEEP_DOG_MICROROS_ENABLE`（默认 0） |
