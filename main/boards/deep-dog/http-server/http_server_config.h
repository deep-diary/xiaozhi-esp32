#ifndef _DEEP_DOG_HTTP_SERVER_CONFIG_H_
#define _DEEP_DOG_HTTP_SERVER_CONFIG_H_

#include "config.h"

/** 局域网 HTTP：控制页 + MJPEG；置 0 关闭以省 internal（默认见 board_features.h） */
#ifndef DEEP_DOG_HTTP_SERVER_ENABLE
#define DEEP_DOG_HTTP_SERVER_ENABLE 0
#endif

/** HTTP 监听端口 */
#ifndef DEEP_DOG_HTTP_SERVER_PORT
#define DEEP_DOG_HTTP_SERVER_PORT 8080
#endif

#endif  // _DEEP_DOG_HTTP_SERVER_CONFIG_H_
