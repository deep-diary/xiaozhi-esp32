"""MQTT / HTTP / WS-MCP 客户端封装（供用例使用）。"""

from __future__ import annotations

import json
import ssl
import threading
import time
from typing import Any, Callable, Optional
from urllib import error as urlerror
from urllib import request as urlrequest


# ==========================================================================
# MQTT
# ==========================================================================

class MqttBus:
    """轻量 MQTT 总线：订阅 + 发布 + 条件等待。"""

    def __init__(self, cfg, client_id: str = "dogtest"):
        import paho.mqtt.client as mqtt
        self.cfg = cfg
        self._lock = threading.Lock()
        self.messages: list[dict] = []
        self.connected = False
        self.last_rc: Any = None
        try:
            self._client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                                       client_id=f"{client_id}-{int(time.time())}")
        except Exception:
            self._client = mqtt.Client(client_id=f"{client_id}-{int(time.time())}")
        if cfg.username:
            self._client.username_pw_set(cfg.username, cfg.password)
        self._client.on_connect = self._on_connect
        self._client.on_message = self._on_message

    def _on_connect(self, c, u, f, rc, props=None):
        rc_val = rc if isinstance(rc, int) else getattr(rc, "value", rc)
        self.last_rc = rc_val
        if rc_val == 0:
            self.connected = True

    def _on_message(self, c, u, msg):
        try:
            payload = msg.payload.decode("utf-8", errors="replace")
        except Exception:
            payload = ""
        with self._lock:
            self.messages.append({
                "topic": msg.topic,
                "payload": payload,
                "retain": bool(getattr(msg, "retain", False)),
                "ts": time.time(),
            })

    def connect(self, timeout: float = 12.0):
        host = self.cfg.broker_host
        try:
            import socket
            host = socket.getaddrinfo(self.cfg.broker_host, self.cfg.broker_port,
                                      socket.AF_INET, socket.SOCK_STREAM)[0][4][0]
        except Exception:
            pass
        self._client.connect(host, self.cfg.broker_port, keepalive=30)
        self._client.loop_start()
        deadline = time.time() + timeout
        while time.time() < deadline and not self.connected and self.last_rc is None:
            time.sleep(0.05)
        if not self.connected:
            raise ConnectionError(f"MQTT connect failed rc={self.last_rc} "
                                  f"({self.cfg.broker_host}:{self.cfg.broker_port})")
        return self

    def subscribe(self, topic: str, qos: int = 1):
        self._client.subscribe(topic, qos=qos)
        return self

    def publish(self, topic: str, payload: Any, qos: int = 1, retain: bool = False):
        body = payload if isinstance(payload, str) else json.dumps(payload, separators=(",", ":"))
        info = self._client.publish(topic, body, qos=qos, retain=retain)
        info.wait_for_publish(timeout=5)
        return body

    def drain(self):
        with self._lock:
            self.messages.clear()

    def get(self, topic: str, since: float = 0.0) -> list[dict]:
        with self._lock:
            return [m for m in self.messages if m["topic"] == topic and m["ts"] >= since]

    def wait_for(self, predicate: Callable[[dict], bool], timeout: float = 10.0,
                 since: float = 0.0) -> Optional[dict]:
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self._lock:
                for m in self.messages:
                    if m["ts"] >= since and predicate(m):
                        return m
            time.sleep(0.05)
        return None

    def wait_topic(self, topic: str, timeout: float = 10.0, since: float = 0.0) -> Optional[dict]:
        return self.wait_for(lambda m: m["topic"] == topic, timeout=timeout, since=since)

    def wait_topic_json(self, topic: str, predicate: Optional[Callable[[dict], bool]] = None,
                        timeout: float = 10.0, since: float = 0.0) -> Optional[dict]:
        def _pred(m):
            if m["topic"] != topic:
                return False
            try:
                data = json.loads(m["payload"])
            except Exception:
                return False
            return predicate(data) if predicate else True
        m = self.wait_for(_pred, timeout=timeout, since=since)
        if not m:
            return None
        return json.loads(m["payload"])

    def close(self):
        try:
            self._client.loop_stop()
            self._client.disconnect()
        except Exception:
            pass


def envelope_data(payload: dict) -> dict:
    """从信封里取业务字段（兼容旧的扁平根对象）。"""
    if isinstance(payload, dict) and isinstance(payload.get("data"), dict):
        return payload["data"]
    return payload if isinstance(payload, dict) else {}


def cmd_body(action: str, **fields) -> dict:
    body = {"action": action, "ts": int(time.time())}
    body.update(fields)
    return body


# ==========================================================================
# HTTP
# ==========================================================================

class TransportAnomaly(Exception):
    """非 HTTP 响应 / 连接级异常（对端未按 HTTP 应答）。

    实测偶发：设备 httpd 在极短时间内回过一个非状态行的首行（如 ``~``），
    urllib 抛 ``BadStatusLine``（见 FINDINGS F-11）。这里统一封装成带原始首行
    的可读异常，便于用例区分「业务失败」与「传输抖动」。
    """


class HttpClient:
    def __init__(self, base: str, timeout: float = 6.0):
        self.base = base.rstrip("/")
        self.timeout = timeout

    def _url(self, path: str) -> str:
        return self.base + path if path.startswith("/") else f"{self.base}/{path}"

    def _once(self, req, timeout: float) -> tuple[int, str]:
        with urlrequest.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read().decode("utf-8", errors="replace")

    def request(self, method: str, path: str, body: Optional[dict] = None,
                timeout: Optional[float] = None) -> tuple[int, str]:
        data = None
        headers = {}
        if body is not None:
            data = json.dumps(body).encode()
            headers["Content-Type"] = "application/json"
        req = urlrequest.Request(self._url(path), data=data, headers=headers, method=method)
        tmo = timeout or self.timeout
        last_exc: Optional[Exception] = None
        for attempt in (1, 2):
            try:
                return self._once(req, tmo)
            except urlerror.HTTPError as e:
                return e.code, e.read().decode("utf-8", errors="replace")
            except Exception as e:  # 传输级异常：重试一次（多设备并发/设备忙时偶发）
                last_exc = e
                if attempt == 1:
                    time.sleep(0.5)
        line = getattr(last_exc, "line", None)
        detail = f"{type(last_exc).__name__}: {last_exc}"
        if line is not None:
            detail += f" (首行={line!r})"
        raise TransportAnomaly(detail) from last_exc

    def get_json(self, path: str, timeout: Optional[float] = None) -> tuple[int, Any]:
        code, text = self.request("GET", path, timeout=timeout)
        try:
            return code, json.loads(text)
        except Exception:
            return code, text

    def post_json(self, path: str, body: Optional[dict] = None,
                  timeout: Optional[float] = None) -> tuple[int, Any]:
        code, text = self.request("POST", path, body=body, timeout=timeout)
        try:
            return code, json.loads(text)
        except Exception:
            return code, text


# ==========================================================================
# WS-MCP
# ==========================================================================

class WsMcp:
    """局域网 WS-MCP 客户端：JSON-RPC tools/list / tools/call。

    使用 ``websockets.sync``（与 scripts/deep_dog/deep_dog_device_audit.py 一致的握手方式）——
    esp_http_server 的 WS 实现与 websocket-client 的握手不兼容（会被 RST）。
    """

    def __init__(self, ws_url: str, timeout: float = 15.0):
        self.ws_url = ws_url
        self.timeout = timeout
        self._ws = None
        self._id = 0

    def connect(self):
        from websockets.sync.client import connect as ws_connect
        self._ws = ws_connect(self.ws_url, open_timeout=self.timeout)
        self._id = 0
        return self

    def is_alive(self) -> bool:
        """连接是否仍可用（设备 httpd 压力下会主动断开，见 FINDINGS F-13）。"""
        try:
            return getattr(self._ws.state, "name", "") == "OPEN"
        except Exception:
            return False

    def _rpc(self, method: str, params: Optional[dict] = None) -> dict:
        """单次 JSON-RPC（**按 id 关联响应**）。

        两个必须处理的设备行为：
        1. 设备把**响应广播给所有已连接客户端**（`TC1308` 明确断言该行为）→ 若同时有其它
           客户端（浏览器设备页 / 其它脚本）在发 `tools/list`，我们可能先收到**别人的响应**；
           实测会导致 `initialize` 收到 tools 列表、分页提前结束（`TC1303/TC1306/TC1311` 假失败，
           见 FINDINGS F-14）。故此处循环读取，**跳过 id 不匹配的帧**直到拿到自己的响应。
        2. 设备 internal SRAM 紧张时 httpd 会超时/断开（F-01/F-13）→ 连接失效时**重连一次**，
           避免「一个断开的 socket 让后续所有 WS 用例连带 ERROR」。
        """
        last_exc: Optional[Exception] = None
        for attempt in (1, 2):
            if not self.is_alive():
                try:
                    self.connect()
                except Exception as exc:
                    last_exc = exc
                    if attempt == 1:
                        time.sleep(0.5)
                        continue
                    raise
            self._id += 1
            want_id = self._id
            env = {"jsonrpc": "2.0", "id": want_id, "method": method}
            if params is not None:
                env["params"] = params
            try:
                self._ws.send(json.dumps(env))
                deadline = time.time() + self.timeout
                while True:
                    remaining = deadline - time.time()
                    if remaining <= 0:
                        raise TimeoutError(f"no response for id={want_id} in {self.timeout}s")
                    raw = self._ws.recv(timeout=remaining)
                    try:
                        msg = json.loads(raw)
                    except Exception:
                        return {"raw": raw}
                    if not isinstance(msg, dict):
                        return {"raw": raw}
                    if msg.get("id") is None:
                        continue  # 通知 / 无 id 帧
                    if str(msg.get("id")) == str(want_id):
                        return msg
                    # 其它客户端的（广播）响应：跳过，继续等自己的 id
            except Exception as exc:
                last_exc = exc
                self._ws = None
                if attempt == 1:
                    time.sleep(0.5)
                    continue
                raise
        raise last_exc if last_exc else RuntimeError("WS-MCP rpc failed")

    def list_tools(self, max_pages: int = 40) -> list[dict]:
        """拉全量工具列表（设备侧按 ~2.8KB 分页，需跟随 nextCursor）。"""
        tools: list[dict] = []
        cursor = ""
        for _ in range(max_pages):
            params: dict = {"withUserTools": True}
            if cursor:
                params["cursor"] = cursor
            resp = self._rpc("tools/list", params)
            result = resp.get("result", {}) or {}
            tools.extend(result.get("tools", []) or [])
            cursor = result.get("nextCursor") or ""
            if not cursor:
                break
        return tools

    def call(self, name: str, arguments: Optional[dict] = None) -> dict:
        return self._rpc("tools/call", {"name": name, "arguments": arguments or {}})

    def close(self):
        try:
            if self._ws is not None:
                self._ws.close()
        except Exception:
            pass
