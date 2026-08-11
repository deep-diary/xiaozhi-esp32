#ifndef _DEEP_DOG_WS_MCP_CONFIG_H_
#define _DEEP_DOG_WS_MCP_CONFIG_H_

#include "config.h"
#include "board_features.h"
#include "face_ai_config.h"

#if DEEP_DOG_HTTP_SERVER_ENABLE
#include "http-server/http_server_config.h"
#endif

#ifndef DEEP_DOG_WS_MCP_ENABLE
#if (DEEP_DOG_MOTOR_ENABLE && !DEEP_DOG_DOG_ENABLE) || DEEP_DOG_FACE_AI_ENABLE
#define DEEP_DOG_WS_MCP_ENABLE 1
#else
#define DEEP_DOG_WS_MCP_ENABLE 0
#endif
#endif

#ifndef DEEP_DOG_WS_MCP_PORT
#define DEEP_DOG_WS_MCP_PORT 8080
#endif

#ifndef DEEP_DOG_WS_MCP_PATH
#define DEEP_DOG_WS_MCP_PATH "/ws"
#endif

/** 单帧最大长度（与 otto 一致） */
#ifndef DEEP_DOG_WS_MCP_MAX_MSG_LEN
#define DEEP_DOG_WS_MCP_MAX_MSG_LEN 4096
#endif

#if DEEP_DOG_HTTP_SERVER_ENABLE && DEEP_DOG_WS_MCP_ENABLE
#if DEEP_DOG_HTTP_SERVER_PORT == DEEP_DOG_WS_MCP_PORT
#error "DEEP_DOG_HTTP_SERVER_PORT and DEEP_DOG_WS_MCP_PORT must differ when both enabled"
#endif
#endif

#endif  // _DEEP_DOG_WS_MCP_CONFIG_H_
