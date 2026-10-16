#pragma once

/**
 * MCP 数据点自动桥接器。
 *
 * 扫描 DataPointRegistry 中标记了 DP_CH_MCP 通道的数据点，
 * 自动生成 self.<module>.get_<point> 和 self.<module>.set_<point> MCP 工具。
 *
 * 文档：docs/interface/04-datapoint-registry.md §3.5
 *
 * 注：复杂过程性操作（如 motor.scan、face.recognize）仍由各模块独立注册，
 *     不进入数据点自动生成体系。
 *
 * **实现方式（F-12 修复）**：不再为每个数据点创建常驻堆对象，而是实现
 * `McpServer::AutoToolSource`——工具名/描述/schema 在 `tools/list`、`tools/call`
 * 时按需生成。因此工具数量与 internal 堆占用**解耦**（此前 75 个工具约 20~30KB
 * 常驻 internal，内存紧张时只能整批跳过注册）。
 *
 * 参考：docs/requirements/catalog/FINDINGS.md F-12
 */

class McpServer;

/**
 * 挂接数据点自动工具源（幂等、**常数内存**，可在开机时无条件调用）。
 *
 * @return 当前注册表中会暴露的工具数量（仅用于日志；后续随点位注册动态变化）
 */
int RegisterDataPointMcpTools(McpServer& mcp_server);
