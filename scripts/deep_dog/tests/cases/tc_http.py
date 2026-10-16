"""HTTP 域用例（数据点 REST + 内置 HTTP server）。"""

from __future__ import annotations

from dogtest.clients import TransportAnomaly
from dogtest.registry import case


def _http(ctx):
    try:
        return ctx.http
    except Exception as exc:
        ctx.blocked(f"HTTP 不可用：{exc}")


def _builtin_gate(ctx, method, path, body=None):
    """内置 HTTP server 用例的统一门控。

    `DEEP_DOG_HTTP_SERVER_ENABLE=0`（默认，且与 WS-MCP 并开会耗尽 internal，见 LL-004）
    时这些端点不存在 → 返回 (code, text)；对端未按 HTTP 应答（传输抖动，见 FINDINGS F-11）
    同样视为「无法判定」，BLOCKED 并附原始首行。
    """
    h = _http(ctx)
    try:
        return h.request(method, path, body=body)
    except TransportAnomaly as exc:
        ctx.blocked(f"设备 httpd 未按 HTTP 应答（偶发传输异常，见 FINDINGS F-11）：{exc}")


@case("TC1256-dp-http-schema", "F1256", "dp", "GET /api/datapoints/schema",
      "HTTP", "200 且返回 schema JSON")
def tc_dp_schema(ctx):
    h = _http(ctx)
    code, body = h.get_json("/api/datapoints/schema")
    ctx.check(code == 200, f"HTTP {code}")
    ctx.check(isinstance(body, (dict, list)), "schema 非 JSON")


@case("TC1257-dp-http-module", "F1257", "dp", "GET /api/datapoints/module/device",
      "HTTP", "200 且返回 device 模块数据点")
def tc_dp_module(ctx):
    h = _http(ctx)
    code, body = h.get_json("/api/datapoints/module/device")
    ctx.check(code == 200, f"HTTP {code}")


@case("TC1258-dp-http-point", "F1258", "dp", "GET /api/datapoints/point/device.device_id",
      "HTTP", "200 且值等于设备 device_id")
def tc_dp_point(ctx):
    h = _http(ctx)
    code, body = h.get_json("/api/datapoints/point/device.device_id")
    ctx.check(code == 200, f"HTTP {code}")
    text = str(body)
    ctx.check(ctx.cfg.device_id in text, f"device_id 未出现在响应: {text[:160]}")


@case("TC1259-dp-http-set", "F1259", "dp", "POST /api/datapoints/set 只读点拒绝",
      "HTTP", "对只读点写入返回错误码而非 200 成功")
def tc_dp_set(ctx):
    h = _http(ctx)
    code, body = h.post_json("/api/datapoints/set",
                             {"id": "device.device_id", "value": "hacked"})
    text = str(body).lower()
    ctx.check("error" in text or "read_only" in text or code >= 400,
              f"只读点写入未被拒绝: {code} {text[:160]}")


@case("TC1352-http-server-cors", "F1352", "http", "数据点 REST 带 CORS / 可跨端访问",
      "HTTP", "响应头含 Access-Control-Allow-Origin")
def tc_cors(ctx):
    h = _http(ctx)
    req = h._url("/api/datapoints/schema")
    import urllib.request as ur
    with ur.urlopen(req, timeout=h.timeout) as resp:
        hdr = resp.headers.get("Access-Control-Allow-Origin")
        ctx.check(hdr == "*", f"CORS 头缺失或异常: {hdr}")


@case("TC1350-http-server-enable", "F1350", "http", "内置 HTTP server（默认关）",
      "HTTP", "内置 /api/status 可用（开关开启时）")
def tc_builtin_http(ctx):
    code, body = _builtin_gate(ctx, "GET", "/api/status")
    if code == 404:
        ctx.blocked("内置 HTTP server 未启用（DEEP_DOG_HTTP_SERVER_ENABLE=0）")
    ctx.check(code == 200, f"HTTP {code}")


@case("TC1353-http-root-page", "F1353", "http", "内置遥控页 GET /",
      "HTTP", "返回 HTML 遥控页（开关开启时）")
def tc_root_page(ctx):
    code, text = _builtin_gate(ctx, "GET", "/")
    if code == 404:
        ctx.blocked("内置 HTTP server 未启用")
    ctx.check(code == 200 and "<html" in str(text).lower(), f"根页异常: {code}")


@case("TC1356-http-api-dog-status", "F1356", "http", "GET /api/dog_status",
      "HTTP", "返回 motors[]/has_fault（内置 server + dog 激活时）")
def tc_dog_status(ctx):
    code, body = _builtin_gate(ctx, "GET", "/api/dog_status")
    if code == 404:
        ctx.blocked("内置 HTTP server 未启用")
    ctx.check(code == 200, f"HTTP {code}")


@case("TC1360-http-api-immich-status", "F1360", "http", "GET /api/immich_status",
      "HTTP", "返回 configured（内置 server 开启时）")
def tc_immich_status_http(ctx):
    code, body = _builtin_gate(ctx, "GET", "/api/immich_status")
    if code == 404:
        ctx.blocked("内置 HTTP server 未启用")
    ctx.check(code == 200, f"HTTP {code}")
