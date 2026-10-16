"""设备与连接配置：CLI / 环境变量 / 串口日志 / MQTT retain 四级自动发现。

字段：
  topic_root    MQTT 话题前缀根（默认 deep-trace）
  device_id     设备稳定 ID（STA MAC 紧凑小写）
  broker_host / broker_port / tls / username / password
  ip            设备当前 IPv4
  ws_url        ws://{ip}:8080/ws（WS-MCP）
  http_base     http://{ip}:8080（数据点 REST / 可选内置 HTTP）
  rtsp_url      rtsp://{broker_host}:8554/deep-dog/{device_id}（MediaMTX）
"""

from __future__ import annotations

import json
import os
import re
import socket
import sys
import time
from dataclasses import dataclass, field

DEFAULT_BROKER_HOST = "10.99.79.54"
DEFAULT_BROKER_PORT = 1883
DEFAULT_TOPIC_ROOT = "deep-trace"
DEFAULT_SERIAL_PORT = "COM6"
DEFAULT_SERIAL_BAUD = 115200
DEFAULT_WS_PORT = 8080


@dataclass
class DeviceConfig:
    topic_root: str = DEFAULT_TOPIC_ROOT
    device_id: str = ""
    broker_host: str = DEFAULT_BROKER_HOST
    broker_port: int = DEFAULT_BROKER_PORT
    tls: bool = False
    username: str = ""
    password: str = ""
    ip: str = ""
    ws_url: str = ""
    http_base: str = ""
    rtsp_url: str = ""
    mqtt_timeout: float = 12.0
    http_timeout: float = 6.0
    serial_port: str = DEFAULT_SERIAL_PORT
    notes: list[str] = field(default_factory=list)

    # ---- 派生 ----
    def prefix(self) -> str:
        return f"{self.topic_root}/{self.device_id}"

    def refresh_endpoints(self) -> None:
        if self.ip:
            if not self.ws_url:
                self.ws_url = f"ws://{self.ip}:{DEFAULT_WS_PORT}/ws"
            if not self.http_base:
                self.http_base = f"http://{self.ip}:{DEFAULT_WS_PORT}"
        if not self.rtsp_url and self.device_id:
            self.rtsp_url = f"rtsp://{self.broker_host}:8554/deep-dog/{self.device_id}"

    def summary(self) -> str:
        lines = [
            f"topic_root : {self.topic_root}",
            f"device_id  : {self.device_id or '(未发现)'}",
            f"broker     : {self.broker_host}:{self.broker_port} tls={self.tls}",
            f"ip         : {self.ip or '(未发现)'}",
            f"ws_url     : {self.ws_url or '(未发现)'}",
            f"http_base  : {self.http_base or '(未发现)'}",
            f"auth       : {'user=' + self.username if self.username else 'anonymous'}",
        ]
        if self.notes:
            lines.append("notes      : " + "; ".join(self.notes))
        return "\n".join(lines)


# --------------------------------------------------------------------------
# 串口日志发现
# --------------------------------------------------------------------------

_RE_BROKER = re.compile(r"broker\s+([0-9A-Za-z\.\-]+):(\d+)\s+tls=(\d+).*?device_id=([0-9a-fA-F]+)")
_RE_GOT_IP = re.compile(r"got ip:\s*([0-9]{1,3}(?:\.[0-9]{1,3}){3})", re.I)
_RE_WS = re.compile(r"ws://([0-9]{1,3}(?:\.[0-9]{1,3}){3}):(\d+)(/\S*)?")


def discover_from_serial(port: str, seconds: float = 25.0, baud: int = DEFAULT_SERIAL_BAUD) -> dict:
    """读串口日志，抽取 broker / device_id / ip / ws。返回部分字段 dict。"""
    try:
        import serial  # pyserial
    except ImportError:
        return {"_error": "pyserial 未安装"}
    found: dict = {}
    buf = ""
    try:
        with serial.Serial(port, baud, timeout=0.5) as ser:
            ser.setDTR(False)
            ser.setRTS(False)
            deadline = time.time() + seconds
            while time.time() < deadline:
                chunk = ser.read(4096)
                if not chunk:
                    continue
                buf += chunk.decode("utf-8", errors="replace")
                # 只保留尾部，避免无界增长
                if len(buf) > 65536:
                    buf = buf[-65536:]
                for m in _RE_BROKER.finditer(buf):
                    found["broker_host"] = m.group(1)
                    found["broker_port"] = int(m.group(2))
                    found["tls"] = m.group(3) == "1"
                    found["device_id"] = m.group(4).lower()
                m = _RE_GOT_IP.search(buf)
                if m:
                    found["ip"] = m.group(1)
                m = _RE_WS.search(buf)
                if m:
                    found["ip"] = found.get("ip") or m.group(1)
                    found["ws_url"] = f"ws://{m.group(1)}:{m.group(2)}{m.group(3) or '/ws'}"
                if found.get("broker_host") and found.get("device_id") and found.get("ip"):
                    break
    except Exception as exc:  # 串口被占用 / 不存在
        return {"_error": f"serial {port}: {exc}"}
    return found


# --------------------------------------------------------------------------
# MQTT retain 发现（不依赖串口）
# --------------------------------------------------------------------------

def discover_from_mqtt(cfg: DeviceConfig, timeout: float = 8.0) -> dict:
    """订阅 `+/+/info/device` retain，反推 topic_root / device_id / ip。"""
    try:
        import paho.mqtt.client as mqtt
    except ImportError:
        return {"_error": "paho-mqtt 未安装"}
    found: dict = {}

    def _mk():
        try:
            return mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                               client_id=f"dogtest-discover-{int(time.time())}")
        except Exception:
            return mqtt.Client(client_id=f"dogtest-discover-{int(time.time())}")

    client = _mk()
    if cfg.username:
        client.username_pw_set(cfg.username, cfg.password)

    def on_connect(c, u, f, rc, props=None):
        c.subscribe("+/+/info/device", qos=0)

    def on_message(c, u, msg):
        parts = msg.topic.split("/")
        if len(parts) < 3:
            return
        found.setdefault("topic_root", parts[0])
        found.setdefault("device_id", parts[1])
        try:
            payload = json.loads(msg.payload.decode("utf-8", errors="replace"))
            data = payload.get("data", payload)
            if isinstance(data, dict) and data.get("ip"):
                found.setdefault("ip", str(data["ip"]))
        except Exception:
            pass

    client.on_connect = on_connect
    client.on_message = on_message
    try:
        host = _resolve(cfg.broker_host)
        client.connect(host, cfg.broker_port, keepalive=15)
    except Exception as exc:
        return {"_error": f"connect {cfg.broker_host}:{cfg.broker_port}: {exc}"}
    client.loop_start()
    deadline = time.time() + timeout
    while time.time() < deadline and not found.get("ip"):
        time.sleep(0.1)
    client.loop_stop()
    try:
        client.disconnect()
    except Exception:
        pass
    return found


def _resolve(host: str) -> str:
    try:
        return socket.getaddrinfo(host, None, socket.AF_INET, socket.SOCK_STREAM)[0][4][0]
    except Exception:
        return host


# --------------------------------------------------------------------------
# 组装
# --------------------------------------------------------------------------

def build(args) -> DeviceConfig:
    """从 argparse 命名空间 + 环境变量 + 自动发现组装 DeviceConfig。"""
    cfg = DeviceConfig()
    cfg.broker_host = args.broker or os.environ.get("DEEP_DOG_MQTT_HOST", DEFAULT_BROKER_HOST)
    cfg.broker_port = int(args.port or os.environ.get("DEEP_DOG_MQTT_PORT", DEFAULT_BROKER_PORT))
    cfg.username = args.username or os.environ.get("DEEP_DOG_MQTT_USER", "")
    cfg.password = args.password or os.environ.get("DEEP_DOG_MQTT_PASS", "")
    cfg.topic_root = args.topic_root or os.environ.get("DEEP_DOG_TOPIC_ROOT", DEFAULT_TOPIC_ROOT)
    cfg.device_id = args.device_id or os.environ.get("DEEP_DOG_DEVICE_ID", "")
    cfg.ip = args.device_ip or os.environ.get("DEEP_DOG_DEVICE_IP", "")
    cfg.serial_port = args.serial or DEFAULT_SERIAL_PORT
    cfg.ws_url = args.ws_url or ""
    cfg.http_base = args.http_base or ""

    if args.discover != "none":
        # auto：先用 MQTT retain 非侵入发现（打开串口会触发 ESP32 自动复位）；
        # 仅在 MQTT 发现不到设备时才回退串口。
        if args.discover in ("auto", "mqtt"):
            d = discover_from_mqtt(cfg, timeout=cfg.mqtt_timeout * 0.6)
            if d.get("_error"):
                cfg.notes.append(d["_error"])
            cfg.topic_root = cfg.topic_root if cfg.device_id else (d.get("topic_root") or cfg.topic_root)
            cfg.device_id = cfg.device_id or d.get("device_id", "")
            cfg.ip = cfg.ip or d.get("ip", "")

        if args.discover == "serial" or (args.discover == "auto"
                                         and (not cfg.device_id or not cfg.ip)):
            d = discover_from_serial(cfg.serial_port, seconds=args.discover_seconds)
            if d.get("_error"):
                cfg.notes.append(d["_error"])
            cfg.broker_host = args.broker or d.get("broker_host") or cfg.broker_host
            cfg.broker_port = int(args.port or d.get("broker_port") or cfg.broker_port)
            cfg.device_id = cfg.device_id or d.get("device_id", "")
            cfg.ip = cfg.ip or d.get("ip", "")
            if d.get("ws_url") and not cfg.ws_url:
                cfg.ws_url = d["ws_url"]

    cfg.refresh_endpoints()
    return cfg


def add_args(ap) -> None:
    ap.add_argument("--broker", default="", help="MQTT broker host（默认取串口/环境/内置默认）")
    ap.add_argument("--port", type=int, default=0, help="MQTT broker 端口")
    ap.add_argument("--username", default="", help="MQTT 用户名")
    ap.add_argument("--password", default="", help="MQTT 密码")
    ap.add_argument("--topic-root", default="", help="话题前缀根，默认 deep-trace")
    ap.add_argument("--device-id", default="", help="设备 ID（MAC 紧凑串）")
    ap.add_argument("--device-ip", default="", help="设备 IP")
    ap.add_argument("--ws-url", default="", help="WS-MCP URL，覆盖自动推导")
    ap.add_argument("--http-base", default="", help="HTTP base，覆盖自动推导")
    ap.add_argument("--serial", default=DEFAULT_SERIAL_PORT, help="串口设备（自动发现用）")
    ap.add_argument("--discover", choices=["auto", "serial", "mqtt", "none"], default="auto",
                    help="设备信息发现策略（默认 auto：串口优先，回退 MQTT retain）")
    ap.add_argument("--discover-seconds", type=float, default=25.0, help="串口发现最长等待秒数")
