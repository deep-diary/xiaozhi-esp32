#!/usr/bin/env python3
"""deep-dog 设备审计：人脸库、内存、任务栈（MQTT / WS MCP）。

示例:
  python3 scripts/deep_dog/deep_dog_device_audit.py --device-ip 192.168.31.211
  python3 scripts/deep_dog/deep_dog_device_audit.py --via web --device-id dev --wait 8
"""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import ssl
import sys
import time
from datetime import datetime
from typing import Any
from urllib.parse import urlparse

try:
    import paho.mqtt.client as mqtt
except ImportError:
    mqtt = None  # type: ignore

try:
    import websockets
except ImportError:
    websockets = None  # type: ignore

WEB_WSS_DEFAULT = "wss://mqtt-ws.deep-diary.com/mqtt"
LAN_HOST_DEFAULT = "192.168.31.25"
WS_PATH_DEFAULT = "/ws"
WS_PORT_DEFAULT = 8080


def ts() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def summarize_face_registry(reg: dict[str, Any]) -> dict[str, Any]:
    entries = reg.get("entries") or reg.get("canonical") or []
    if isinstance(reg, dict) and "count" in reg and not entries:
        return {"canonical_count": reg.get("count"), "raw": reg}
    canonical = 0
    alias_total = 0
    names: list[str] = []
    for e in entries:
        if not isinstance(e, dict):
            continue
        canonical += 1
        aliases = e.get("aliases") or []
        if isinstance(aliases, list):
            alias_total += len(aliases)
        name = e.get("display_name") or f"#{e.get('local_id', '?')}"
        names.append(str(name))
    feat_estimate = canonical + alias_total
    return {
        "canonical_count": canonical,
        "alias_embeddings": alias_total,
        "feat_slots_estimate": feat_estimate,
        "display_names": names,
    }


def mqtt_collect(prefix: str, via: str, wss: str, broker: str, port: int, user: str, pwd: str, wait_s: float) -> dict[str, Any]:
    if mqtt is None:
        return {"error": "missing paho-mqtt"}
    out: dict[str, Any] = {}
    topics = [f"{prefix}/face/registry", f"{prefix}/device/status", f"{prefix}/device/info"]

    def on_connect(client, userdata, flags, reason_code, properties=None):
        rc = reason_code if isinstance(reason_code, int) else getattr(reason_code, "value", reason_code)
        if rc != 0:
            out["connect_error"] = rc
            return
        for t in topics:
            client.subscribe(t)

    def on_message(client, userdata, msg):
        key = msg.topic.split("/")[-1]
        try:
            out[key] = json.loads(msg.payload.decode())
        except Exception as e:
            out[key] = {"_parse_error": str(e), "_raw": msg.payload[:300].decode(errors="replace")}

    if via == "web":
        try:
            client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"audit_{int(time.time())}", transport="websockets")
        except Exception:
            client = mqtt.Client(client_id=f"audit_{int(time.time())}", transport="websockets")
        u = urlparse(wss)
        client.ws_set_options(path=u.path or "/mqtt")
        client.tls_set(cert_reqs=ssl.CERT_REQUIRED)
        host, port_n = u.hostname, u.port or 443
    else:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"audit_{int(time.time())}")
        host, port_n = broker, port

    client.on_connect = on_connect
    client.on_message = on_message
    if user:
        client.username_pw_set(user, pwd)
    try:
        client.connect(host, port_n, 60)
        client.loop_start()
        time.sleep(wait_s)
        client.loop_stop()
        client.disconnect()
    except Exception as e:
        out["connect_exception"] = str(e)
    return out


async def ws_mcp_call(host: str, port: int, path: str, tool: str, arguments: dict | None = None) -> Any:
    if websockets is None:
        return {"error": "missing websockets"}
    uri = f"ws://{host}:{port}{path}"
    req = {
        "jsonrpc": "2.0",
        "method": "tools/call",
        "params": {"name": tool, "arguments": arguments or {}},
        "id": 1,
    }
    try:
        async with websockets.connect(uri, open_timeout=6) as ws:
            await ws.send(json.dumps(req))
            raw = await asyncio.wait_for(ws.recv(), timeout=8)
            outer = json.loads(raw)
            content = outer.get("result", {}).get("content") or []
            if content and content[0].get("type") == "text":
                return json.loads(content[0]["text"])
            return outer
    except Exception as e:
        return {"error": str(e)}


def summarize_tasks(status: dict[str, Any]) -> dict[str, Any]:
    tasks = status.get("tasks")
    if not isinstance(tasks, list):
        return {"task_count": status.get("task_count"), "tasks": None}
    internal_hwm = 0
    psram_hint = 0
    rows = []
    for t in tasks:
        if not isinstance(t, dict):
            continue
        name = str(t.get("name", ""))
        hwm = int(t.get("stack_hwm") or 0)
        rows.append({"name": name, "stack_hwm": hwm, "prio": t.get("prio"), "state": t.get("state")})
        if any(k in name for k in ("vision_hub", "mjpeg", "stream_photo")):
            psram_hint += hwm
        else:
            internal_hwm += hwm
    rows.sort(key=lambda x: x["stack_hwm"], reverse=True)
    return {
        "task_count": len(rows),
        "stack_hwm_sum_bytes": internal_hwm + psram_hint,
        "stack_hwm_internal_estimate": internal_hwm,
        "top_tasks": rows[:15],
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="deep-dog device audit (face registry, mem, tasks)")
    ap.add_argument("--device-id", default=os.environ.get("DEEP_DOG_DEVICE_ID", "dev"))
    ap.add_argument("--device-ip", default=os.environ.get("DEEP_DOG_DEVICE_IP", "192.168.31.211"))
    ap.add_argument("--ws-port", type=int, default=int(os.environ.get("DEEP_DOG_WS_MCP_PORT", WS_PORT_DEFAULT)))
    ap.add_argument("--ws-path", default=WS_PATH_DEFAULT)
    ap.add_argument("--via", choices=["web", "lan", "none"], default="web")
    ap.add_argument("--wss", default=WEB_WSS_DEFAULT)
    ap.add_argument("--broker", default=LAN_HOST_DEFAULT)
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--username", default=os.environ.get("DEEP_DOG_MQTT_USER", ""))
    ap.add_argument("--password", default=os.environ.get("DEEP_DOG_MQTT_PASS", ""))
    ap.add_argument("--wait", type=float, default=8.0)
    ap.add_argument("--json-out", default="")
    args = ap.parse_args()

    prefix = f"deepdiary/deep-dog/{args.device_id}"
    report: dict[str, Any] = {"ts": ts(), "device_id": args.device_id, "device_ip": args.device_ip}

    if args.via != "none":
        report["mqtt"] = mqtt_collect(prefix, args.via, args.wss, args.broker, args.port, args.username, args.password, args.wait)
        reg = report["mqtt"].get("registry") or report["mqtt"].get("face/registry")
        if isinstance(reg, dict):
            report["face_summary"] = summarize_face_registry(reg)
        st = report["mqtt"].get("status")
        if isinstance(st, dict):
            report["mem"] = st.get("mem")
            report["task_summary"] = summarize_tasks(st)

    if websockets is not None:
        diag = asyncio.run(ws_mcp_call(args.device_ip, args.ws_port, args.ws_path, "self.board.diagnostics"))
        report["ws_diagnostics"] = diag
        if isinstance(diag, dict):
            report["mem"] = report.get("mem") or diag.get("mem")
            report["task_summary"] = report.get("task_summary") or summarize_tasks(diag)
        face_list = asyncio.run(ws_mcp_call(args.device_ip, args.ws_port, args.ws_path, "self.face.list", {"include_live": True}))
        report["ws_face_list"] = face_list
        if isinstance(face_list, dict) and "entries" in face_list:
            report["face_summary_ws"] = summarize_face_registry(face_list)

    text = json.dumps(report, ensure_ascii=False, indent=2)
    print(text)
    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            f.write(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
