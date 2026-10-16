# catalog · 功能与用例注册表

本目录是 deep-dog **「功能点 ↔ 测试用例 ↔ 自动化脚本 ↔ 测试结果」** 的唯一索引层。

| 文件 | 内容 |
|------|------|
| [FEATURES.md](./FEATURES.md) | 全仓库功能点注册表（`F<NNNN>-<domain>-<slug>`） |
| [TESTS.md](./TESTS.md) | 测试用例注册表（`TC<NNNN>-<domain>-<slug>`） |
| [FINDINGS.md](./FINDINGS.md) | 全盘测试暴露的**缺陷与契约冲突**（含证据与修复建议） |
| [../TEST_REPORT.md](../TEST_REPORT.md) | 最近一次全盘测试报告（运行生成） |

规则见 [CONVENTIONS.md §2.1](../../CONVENTIONS.md)。要点：

- `F/TC` 与既有路线图 ID（M01 / V-S03 / MOT-01 / CFG-01 …）**并存**：路线图表交付批次，F/TC 表可验证的最小功能单元。
- 功能点正文仍在各域需求文档；本注册表只做**编号 + 归属 + 追溯**，不复制正文（单一真源）。
- 自动化脚本位于 `scripts/deep_dog/tests/`，用例 ID 与 `TC` 编号一一对应。
