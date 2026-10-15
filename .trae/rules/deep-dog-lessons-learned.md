---
description: deep-dog 开发/联调中踩到可复用的坑时，将结论沉淀到 docs/ll 便于后续查阅
alwaysApply: true
---

# deep-dog：经验教训记录（Lessons Learned）

在 deep-dog 开发与联调中，**遇到排查耗时、易复现、结论可复用的坑**时，除修复代码外，**必须**把结论记录到经验库，避免重复踩坑。

## 记录位置

`main/boards/deep-dog/docs/ll/`

- 目录约定、模板、索引见 `main/boards/deep-dog/docs/ll/README.md`。
- 新经验以 `LL-NNN-<slug>.md` 命名（三位序号全局递增），并更新 `README.md` 的「索引」。格式与模板见 `main/boards/deep-dog/docs/CONVENTIONS.md`。

## 何时记录（满足任一条）

- 排查耗时长、结论非显而易见；
- 涉及编译 / 烧录 / 硬件 / 环境类问题，复现成本高；
- 属于「配置 / 环境 / 流程」类，而非一次性代码笔误；
- 有明确「下次该怎么做」的正确做法。

## 每条经验必备结构

- **现象**：日志 / 报错 / 表现
- **根因**：一句话说清
- **正确做法**：可直接照做的命令或改法
- **相关文件**：涉及的源码 / 配置路径

## 自检

- [ ] 本次是否有值得沉淀的坑？
- [ ] 有则已写入 `docs/ll/LL-NNN-*.md` 并更新索引
- [ ] 未记录敏感信息（密码 / Token / 密钥）
