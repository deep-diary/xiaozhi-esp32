#pragma once

class McpServer;
class LedStripControl;

/** 注册 self.led_strip.* 语音 MCP 工具；经 LedStripControl 改灯效，MQTT status 同步 */
void RegisterLedMcpTools(McpServer& mcp_server, LedStripControl* control);
