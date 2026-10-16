"""deep-dog 自动化测试框架（dogtest）。

模块：
  config   设备/连接配置（CLI / 环境变量 / 串口自动发现 / MQTT retain 发现）
  registry 用例注册表 + @case 装饰器
  clients  MQTT / HTTP / WS-MCP 客户端封装
  report   结果收集 + JSON / Markdown 报告
  runner   发现 → 过滤 → 执行 → 报告

测试用例在 ``cases/`` 下按域分文件，用例 ID 对齐
``docs/requirements/catalog/TESTS.md`` 的 ``TC<NNNN>-<domain>-<slug>``。
"""

__all__ = ["config", "registry", "clients", "report", "runner"]
