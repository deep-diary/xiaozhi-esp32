#pragma once

/**
 * HTTP 统一数据点端点（/api/datapoints/...）。
 *
 * 文档：docs/interface/04-datapoint-registry.md §3.4
 *
 * 端点列表：
 *   GET  /api/datapoints/schema          - 完整 schema JSON
 *   GET  /api/datapoints/module/<name>   - 指定模块所有点的当前值
 *   GET  /api/datapoints/point/<id>      - 单个数据点当前值
 *   POST /api/datapoints/set             - 写入单个数据点
 *   POST /api/datapoints/batch           - 批量读取
 *   POST /api/datapoints/batch_set       - 批量写入
 */

#include <esp_http_server.h>

/**
 * 注册所有 /api/datapoints/... 端点到指定 HTTP server。
 * 由 DeepDogHttpServer::Start() 调用。
 */
bool RegisterDataPointHttpEndpoints(httpd_handle_t server);
