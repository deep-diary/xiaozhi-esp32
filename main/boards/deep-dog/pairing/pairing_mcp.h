#pragma once

class DeepDogMqtt;
class McpServer;

void RegisterPairingMcpTools(McpServer& mcp_server, DeepDogMqtt* mqtt);
