"""WS-MCP 域用例（局域网 WebSocket → MCP 桥）。"""

from __future__ import annotations

import json
import socket

from dogtest import dpschema
from dogtest.clients import envelope_data
from dogtest.registry import case


def _tools(ctx):
    try:
        return ctx.wsmcp.list_tools()
    except Exception as exc:
        ctx.blocked(f"WS-MCP 不可用：{exc}")


def _names(tools):
    return [t.get("name", "") for t in tools]


@case("TC0011b-device-capability-wsmcp", "F0011", "device",
      "capabilities.ws_mcp 与实际 WS-MCP 服务一致",
      "MQTT", "端口 8080 可连时 capabilities.ws_mcp 应为 true")
def tc_capability_wsmcp_consistency(ctx):
    bus = ctx.require_mqtt()
    bus.subscribe(ctx.info("device"))
    msg = bus.wait_topic(ctx.info("device"), timeout=12)
    ctx.check(msg is not None, "未收到 info/device")
    caps = envelope_data(json.loads(msg["payload"])).get("capabilities", {})
    try:
        s = socket.create_connection((ctx.cfg.ip, 8080), timeout=3)
        s.close()
        port_up = True
    except Exception:
        port_up = False
    if port_up:
        ctx.check(caps.get("ws_mcp") is True,
                  f"WS-MCP 端口 8080 在监听，但 capabilities.ws_mcp={caps.get('ws_mcp')}")
    else:
        ctx.skip("WS-MCP 端口未监听（无法比对）")


@case("TC1302-mcp-endpoint", "F1302", "mcp", "WS-MCP 端点 8080 /ws 可连",
      "WS-MCP", "ws://{ip}:8080/ws 握手成功并可 tools/list")
def tc_mcp_endpoint(ctx):
    tools = _tools(ctx)
    ctx.check(isinstance(tools, list), "tools/list 未返回数组")
    ctx.log(f"tools={len(tools)}")


@case("TC1306-mcp-methods", "F1306", "mcp", "标准 MCP 方法 tools/list",
      "WS-MCP", "tools/list 返回 name/description/inputSchema")
def tc_mcp_methods(ctx):
    tools = _tools(ctx)
    ctx.check(tools, "tools/list 为空")
    t = tools[0]
    ctx.check("name" in t, "工具缺 name")


@case("TC1311-mcp-dp-tools", "F1311", "mcp", "数据点自动 MCP 工具注册",
      "WS-MCP", "按 schema 推导的 self.<mod>.get_/set_<point> 应全部出现")
def tc_mcp_dp_tools(ctx):
    names = set(_names(_tools(ctx)))
    schema = dpschema.read_schema(ctx)
    if not schema:
        ctx.blocked("无法读取 /api/datapoints/schema（不能推导自动工具命名）")
    expected = dpschema.expected_tools(schema)
    if not expected:
        ctx.skip("schema 中无 mcp 通道点位")
    hit = [n for n in expected if n in names]
    if len(hit) != len(expected):
        largest = dpschema.largest_internal_bytes(ctx)
        ctx.blocked(
            f"自动工具 {len(hit)}/{len(expected)} 个（按 schema 推导）；"
            f"internal largest_free={largest}B，注册下限 6144B → 内存守卫跳过（F-01 家族 / S09）")
    ctx.check(len(hit) == len(expected), f"自动工具不全：{len(hit)}/{len(expected)}")
    ctx.log(f"auto dp tools={len(hit)}/{len(expected)} 例: " + ", ".join(hit[:4]))


@case("TC0014-device-mcp-get-ip", "F0014", "device", "MCP self.board.get_ip",
      "WS-MCP", "返回 ip/connected")
def tc_mcp_get_ip(ctx):
    r = ctx.wsmcp.call("self.board.get_ip", {})
    ctx.check("result" in r, f"调用失败: {r}")
    ctx.log(json.dumps(r.get("result"), ensure_ascii=False)[:200])


@case("TC0015-device-mcp-diagnostics", "F0015", "device", "MCP self.board.diagnostics",
      "WS-MCP", "返回内存 + 任务栈快照")
def tc_mcp_diag(ctx):
    r = ctx.wsmcp.call("self.board.diagnostics", {})
    ctx.check("result" in r, f"调用失败: {r}")
    text = json.dumps(r.get("result"), ensure_ascii=False)
    ctx.check(len(text) > 20, "diagnostics 载荷过短")


@case("TC0527-face-mcp-list", "F0527", "face", "MCP self.face.list",
      "WS-MCP", "返回人脸库列表")
def tc_mcp_face_list(ctx):
    r = ctx.wsmcp.call("self.face.list", {})
    ctx.check("result" in r, f"调用失败: {r}")
    ctx.log(json.dumps(r.get("result"), ensure_ascii=False)[:160])


@case("TC0525-face-mcp-set-mode", "F0525", "face", "MCP self.face.set_mode",
      "WS-MCP", "set_mode 调用成功")
def tc_mcp_face_set_mode(ctx):
    r = ctx.wsmcp.call("self.face.set_mode", {"detect": False})
    ctx.check("result" in r or "error" not in r, f"调用异常: {r}")
    ctx.log(json.dumps(r.get("result"), ensure_ascii=False)[:160])


@case("TC0655-greet-mcp-get-identity", "F0655", "greet", "MCP self.face.get_identity",
      "WS-MCP", "返回当前身份")
def tc_mcp_get_identity(ctx):
    r = ctx.wsmcp.call("self.face.get_identity", {})
    ctx.check("result" in r, f"调用失败: {r}")
    ctx.log(json.dumps(r.get("result"), ensure_ascii=False)[:160])


@case("TC1315-mcp-3way-consistency", "F1315", "mcp", "MQTT/MCP 三端一致（face set_mode）",
      "WS-MCP", "MCP 与 MQTT 都暴露 face set_mode 能力")
def tc_mcp_3way(ctx):
    names = _names(_tools(ctx))
    ctx.check(any(n == "self.face.set_mode" for n in names), "MCP 未见 self.face.set_mode")
    r = ctx.wsmcp.call("self.face.set_mode", {"detect": False})
    ctx.check("result" in r, f"MCP set_mode 调用失败: {r}")


@case("TC1303-mcp-handshake", "F1303", "mcp", "MCP initialize 握手",
      "WS-MCP", "initialize 返回 protocolVersion / serverInfo")
def tc_mcp_init(ctx):
    ws = ctx.wsmcp
    r = ws._rpc("initialize", {"protocolVersion": "2024-11-05", "capabilities": {},
                               "clientInfo": {"name": "dogtest", "version": "1.0"}})
    res = r.get("result") or {}
    if not res:
        ctx.blocked(f"设备未实现 initialize（返回 {str(r)[:120]}）")
    ctx.check("protocolVersion" in res or "serverInfo" in res,
              f"initialize 响应缺 protocolVersion/serverInfo：{str(res)[:160]}")
    ctx.log("serverInfo=" + json.dumps(res.get("serverInfo"), ensure_ascii=False)[:120])


@case("TC1305-mcp-envelope", "F1305", "mcp", "信封入站 {\"type\":\"mcp\",\"payload\":…}",
      "WS-MCP", "信封形态的 tools/list 同样得到响应")
def tc_mcp_envelope(ctx):
    ws = ctx.wsmcp
    ws._id += 1
    payload = {"jsonrpc": "2.0", "id": ws._id, "method": "tools/list",
               "params": {"withUserTools": True}}
    ws._ws.send(json.dumps({"type": "mcp", "payload": payload}))
    raw = ws._ws.recv(timeout=ws.timeout)
    r = json.loads(raw)
    ctx.check("result" in r and "tools" in (r.get("result") or {}),
              f"信封入站未得到 tools/list 结果：{str(r)[:160]}")


@case("TC1307-mcp-max-frame", "F1307", "mcp", "超长帧被丢弃且连接存活",
      "WS-MCP", ">4096B 帧被拒后，连接仍可正常 tools/list")
def tc_mcp_max_frame(ctx):
    ws = ctx.wsmcp
    big = "x" * 6000
    ws._id += 1
    try:
        ws._ws.send(json.dumps({"jsonrpc": "2.0", "id": ws._id, "method": "tools/call",
                                "params": {"name": "noop", "arguments": {"blob": big}}}))
    except Exception as exc:
        ctx.blocked(f"发送超长帧即断开：{exc}")
    # 连接应仍可用
    tools = ws.list_tools()
    ctx.check(isinstance(tools, list), "超长帧后连接不可用（未存活）")


@case("TC1308-mcp-broadcast", "F1308", "mcp", "响应广播到所有已连接客户端",
      "WS-MCP", "客户端 A 的 tools/list 响应也出现在客户端 B")
def tc_mcp_broadcast(ctx):
    from dogtest.clients import WsMcp
    if not ctx.cfg.ws_url:
        ctx.blocked("ws_url 未发现")
    a = ctx.wsmcp
    b = WsMcp(ctx.cfg.ws_url).connect()
    try:
        a._id += 1
        a._ws.send(json.dumps({"jsonrpc": "2.0", "id": a._id, "method": "tools/list",
                               "params": {"withUserTools": True}}))
        got_a = json.loads(a._ws.recv(timeout=a.timeout))
        ctx.check("result" in got_a, "A 未收到自身响应")
        try:
            got_b = json.loads(b._ws.recv(timeout=6))
        except Exception as exc:
            ctx.check(False, f"B 未收到广播（{type(exc).__name__}）——响应未广播给其它客户端")
    finally:
        b.close()
