#pragma once

#include "face_ai_config.h"

class McpServer;

#if DEEP_DOG_FACE_AI_ENABLE
void RegisterFaceMcpTools(McpServer& mcp_server);
#else
inline void RegisterFaceMcpTools(McpServer&) {}
#endif
