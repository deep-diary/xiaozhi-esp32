# ll — Lessons Learned（经验教训）

本目录用于沉淀 deep-dog 开发/联调中**踩过的坑**，便于后续快速查阅、避免重复踩坑。

## 什么时候记

满足以下**任一条**就应记录（避免把「一眼可见的小笔误」也写进来）：

- 排查耗时较长、结论非显而易见；
- 容易再次复现、且复现成本高（涉及编译/烧录/硬件/环境）；
- 属于「配置 / 环境 / 流程」类问题，而非一次性代码笔误；
- 结论能直接指导「下次该怎么做」——有明确正确做法。

## 怎么记（约定）

1. **命名**：`LL-NNN-<short-slug>.md`，`NNN` 三位序号、全局递增（从 `001` 开始），不复用旧号。
2. **必备结构**（每条坑一小节）：
   - **现象**：看到什么日志 / 报错 / 表现。
   - **根因**：为什么发生（一句话说清）。
   - **正确做法**：可直接照做的命令或改法。
   - **相关文件**：涉及的源码 / 配置路径。
3. **编码**：文件名用 ASCII，正文用中文；命令用代码块。
4. **挂索引**：新增后在本 `README.md` 的「索引」更新一行。
5. **不写敏感信息**：密码、Token、密钥一律不写。

## 模板

```markdown
## LL-NNN 一句话标题

- **现象**：...
- **根因**：...
- **正确做法**：
  ```bash
  ...
  ```
- **相关文件**：`path/to/file`
```

## 索引

| 编号 | 标题 | 关键词 |
|------|------|--------|
| [LL-001](./LL-001-board-config-and-bringup.md) | 板级编译配置与联调踩坑合集 | sdkconfig / config.json / OV3660 / 串口占用 / IDF 环境 / MQTT 默认值 / WinError 32 / ext_pins 模式(PWM/CAN) / DEEP_DOG_*_ENABLE 宏未定义 |
| [LL-002](./LL-002-board-type-not-switched.md) | 板型未切换导致编出别的板 | BOARD_TYPE / sdkconfig / 编错板 / SKU=deep-dog |
| [LL-003](./LL-003-ccache-cmake-cxx-features.md) | ccache 导致 CMake CXX 特性探测失败 + 新增源需 reconfigure | ccache / target_compile_features / GLOB / undefined reference |
| [LL-004](./LL-004-httpd-and-datapoint-http.md) | HTTP/WS 服务与 ESP-IDF httpd 踩坑 | uri_match_fn / 通配符 URI 404 / ESP_ERR_HTTPD_TASK / internal 内存 / 复用 httpd |
| [LL-004](./LL-004-restore-headers-and-union-size-measure.md) | 还原头文件后增量编译不生效 + 全模式固件体积实测方法 | Copy-Item mtime / ninja 假增量 / 全集体积 / nm 符号校验 |
