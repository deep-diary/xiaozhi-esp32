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
 */

class McpServer;

/**
 * 为所有带 DP_CH_MCP 标志的数据点自动注册 MCP get/set 工具。
 * 调用方在 MCP server 启动后、各模块注册完自定义工具后调用。
 *
 * @param mcp_server  MCP server 实例
 * @return            自动注册的工具数量
 */
int RegisterDataPointMcpTools(McpServer& mcp_server);
