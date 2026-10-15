# LL-004 · HTTP/WS 服务与 ESP-IDF httpd 踩坑

## LL-004-1 ESP-IDF httpd 默认精确匹配，通配符 URI 一律 404

- **现象**：
  注册了 `/api/datapoints/module/*` 这类以 `*` 结尾的通配符 URI 后，
  访问 `/api/datapoints/module/imu` 返回 **404**；而同 httpd 上的**精确** URI（如
  `/api/datapoints/schema`）却正常返回 200。容易误判为 handler 逻辑错误。

- **根因**：
  `HTTPD_DEFAULT_CONFIG()` 的 `uri_match_fn` 默认为 `NULL`，此时 esp_http_server 只做**精确匹配**，
  URI 中以 `*` 结尾的通配符**不生效**。

- **正确做法**：
  创建 httpd 时显式启用通配符匹配：
  ```c
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.uri_match_fn = httpd_uri_match_wildcard;
  ```
  注意：启用后，无 `*` 的 URI（如 `/ws`）仍按精确匹配，行为不变。

- **相关文件**：`main/boards/deep-dog/ws-mcp/deep_dog_ws_mcp_server.cc`、
  `main/boards/deep-dog/http-server/deep_dog_http_server.cc`、
  `main/boards/deep-dog/http-server/datapoint_http.cc`

---

## LL-004-2 internal 内存不足以并存两个 httpd（HTTP server + WS MCP）

- **现象**：
  启用独立 HTTP server（`DEEP_DOG_HTTP_SERVER_ENABLE=1`）后，两者**双双启动失败**：
  ```
  E dog_http: httpd_start failed
  W dog_ws_mcp: httpd_start attempt 1/3 failed port=8080 err=ESP_ERR_HTTPD_TASK free_int=12799 largest_int=3840
  E httpd: httpd_server_init: error in listen (112)
  ```
  原本可用的 WS MCP 也被拖挂（回归）。

- **根因**：
  deep-dog 的 internal 余量本就紧张；再起一个 httpd 需要为其任务分配**连续**栈（默认约 4KB），
  当时 `largest_int=3840 < 4096` → `ESP_ERR_HTTPD_TASK`；且一个 httpd 的失败/占用会进一步挤压
  内存，使后续 WS MCP 也起不来。

- **正确做法**：
  **不要并存两个 httpd**。数据点 REST 端点复用 WS MCP 的 httpd（同端口多路由）：

  ```cpp
  // esp_sparkbot_board.cc：WS MCP 启动成功后
  RegisterDataPointHttpEndpoints(ws_mcp.HttpdHandle());
  ```
  实测开销可忽略：挂载前 `free=24823/largest=7168`，挂载后 `free=24807/largest=7168`。

- **相关文件**：`main/boards/deep-dog/board_features.h`（HTTP server 默认置 0 的原因注释）、
  `main/boards/deep-dog/ws-mcp/deep_dog_ws_mcp_server.h`（`HttpdHandle()`）、
  `main/boards/deep-dog/esp_sparkbot_board.cc`（挂载点）
