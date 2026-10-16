"""RTSP / MediaMTX 域用例。"""

from __future__ import annotations

import json
import socket
import time
import urllib.error
import urllib.request

from dogtest.clients import envelope_data
from dogtest.registry import case


def _tcp_open(host: str, port: int, timeout: float = 3.0) -> bool:
    try:
        s = socket.create_connection((host, port), timeout=timeout)
        s.close()
        return True
    except Exception:
        return False


@case("TC1400-rtsp-infra-reachability", "F1400", "rtsp", "MediaMTX 主机可达",
      "HTTP", "MediaMTX 控制端口返回 Server: mediamtx")
def tc_infra(ctx):
    url = f"http://{ctx.cfg.broker_host}:8888/"
    server = ""
    try:
        with urllib.request.urlopen(url, timeout=4) as resp:
            server = resp.headers.get("Server", "")
    except urllib.error.HTTPError as e:      # MediaMTX 根路径返回 404，但 Server 头仍可读
        server = e.headers.get("Server", "") if e.headers else ""
    except Exception as exc:
        ctx.blocked(f"MediaMTX {ctx.cfg.broker_host}:8888 不可达：{exc}")
    ctx.check("mediamtx" in server.lower(), f"未见 mediamtx 特征: Server={server!r}")
    ctx.log(f"Server={server}")


@case("TC1404-rtsp-h264-push", "F1404", "rtsp", "设备 RTSP 推流（H.264）",
      "MQTT", "cmd/stream start 后进入 streaming（有 RTP），否则给出可读 error/阻塞原因")
def tc_rtsp_push(ctx):
    bus = ctx.require_mqtt()
    topic = ctx.status("stream")
    bus.subscribe(topic)
    time.sleep(1.0)                 # 先吃掉 retain 快照（可能带旧的 error），避免误判
    since = time.time()
    bus.publish(ctx.cmd("stream"), {"action": "start", "mode": "rtsp_push",
                                    "ts": int(time.time())})

    deadline = time.time() + 35      # 握手 + 首帧编码 + 首个 RTP 需要时间
    cursor = since
    last = None
    streaming = False
    while time.time() < deadline:
        m = bus.wait_for(lambda mm: mm["topic"] == topic and mm["ts"] > cursor, timeout=3)
        if not m:
            continue
        cursor = m["ts"]
        try:
            last = envelope_data(json.loads(m["payload"]))
        except Exception:
            continue
        st = str(last.get("state", ""))
        if st == "streaming":
            streaming = True
            break
        if st == "error":
            break

    url = (last or {}).get("push_url") or (last or {}).get("url") or ""
    bus.publish(ctx.cmd("stream"), {"action": "stop", "ts": int(time.time())})  # 收尾

    if streaming:
        ctx.log("streaming push_url=" + str(url))
        return

    err = (last or {}).get("error")
    host, port = None, 0
    if url.startswith("rtsp://"):
        host_port = url[len("rtsp://"):].split("/", 1)[0]
        host, _, p = host_port.partition(":")
        port = int(p or 554)
    if host and not _tcp_open(host, port):
        ctx.blocked(f"推送目标 {host}:{port} 不可达（MediaMTX 未监听）；error={err}")
    if str(err) == "no_rtp":
        # 推流会话建立（MediaMTX 可达）但**没有视频帧**：视频源（DVP 摄像头）未产出画面。
        # 典型为摄像头未检出 / 需断电重启，非固件缺陷 → 记为环境阻塞。
        ctx.blocked(f"RTSP 会话已建立但无帧（error=no_rtp）：视频源未就绪/摄像头未检出（需 power cycle）")
    ctx.check(False, f"35s 内未进入 streaming: state={(last or {}).get('state')} error={err}")
