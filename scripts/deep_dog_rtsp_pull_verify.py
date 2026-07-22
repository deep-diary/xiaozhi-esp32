#!/usr/bin/env python3
"""Pull MediaMTX RTSP for ~5s and save media/frames under fixtures/rtsp_pull_verify.

Prefers ffmpeg; falls back to a minimal TCP interleaved RTSP JPEG client when
ffmpeg cannot reach :8554 (common intermittent EHOSTUNREACH on some Mac routes).

Example:
  python3 scripts/deep_dog_rtsp_pull_verify.py
  python3 scripts/deep_dog_rtsp_pull_verify.py --outdir h264
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import socket
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / "main/boards/deep-dog/swrs/vision/fixtures/rtsp_pull_verify"
DEFAULT_DEVICE = "http://192.168.31.211:8080"
DEFAULT_RTSP = "rtsp://192.168.31.25:8554/deep-dog/dev"


def http_json(url: str, method: str = "GET", timeout: float = 5.0) -> dict:
    req = urllib.request.Request(url, method=method)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        raw = resp.read().decode("utf-8", errors="replace")
        return json.loads(raw) if raw else {}


def ensure_push(device_base: str, timeout_s: float = 30.0) -> dict:
    base = device_base.rstrip("/")
    try:
        http_json(f"{base}/api/vision_publish?mode=rtsp_push", method="POST")
    except urllib.error.URLError as e:
        raise SystemExit(f"enable push failed: {e}") from e

    deadline = time.time() + timeout_s
    last: dict = {}
    while time.time() < deadline:
        try:
            last = http_json(f"{base}/api/status")
        except urllib.error.URLError:
            time.sleep(1.0)
            continue
        status = last.get("push_status")
        mode = last.get("mode") or last.get("publish")
        print(f"  status mode={mode} push_status={status} has_jpeg={last.get('has_jpeg')}")
        if mode == "rtsp_push" and status == "streaming":
            return last
        time.sleep(1.0)
    raise SystemExit(f"timeout waiting for push_status=streaming; last={last}")


def run_ffmpeg(args: list[str]) -> None:
    print("+", " ".join(args))
    proc = subprocess.run(args, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr or proc.stdout or "(no ffmpeg output)\n")
        raise RuntimeError(f"ffmpeg failed with code {proc.returncode}")


def _recv_until(sock: socket.socket, marker: bytes, limit: int = 65536) -> bytes:
    buf = b""
    while marker not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            raise ConnectionError("socket closed while reading RTSP headers")
        buf += chunk
        if len(buf) > limit:
            raise ConnectionError("RTSP header too large")
    return buf


def _rtsp_exchange(sock: socket.socket, req: str) -> tuple[int, bytes, bytes]:
    sock.sendall(req.encode("ascii"))
    raw = _recv_until(sock, b"\r\n\r\n")
    header, rest = raw.split(b"\r\n\r\n", 1)
    text = header.decode("latin1", errors="replace")
    m = re.match(r"RTSP/\S+\s+(\d+)", text)
    if not m:
        raise ConnectionError(f"bad RTSP response: {text[:120]!r}")
    code = int(m.group(1))
    cl = 0
    for line in text.split("\r\n"):
        if line.lower().startswith("content-length:"):
            cl = int(line.split(":", 1)[1].strip())
    body = rest
    while len(body) < cl:
        body += sock.recv(cl - len(body))
    return code, header, body[:cl]


def _jpeg_from_rfc2435_payload(data: bytes) -> bytes | None:
    """Extract JFIF from concatenated RFC2435 JPEG payloads (may include Q=255 DQT hdr)."""
    i = data.find(b"\xff\xd8")
    if i < 0:
        return None
    j = data.find(b"\xff\xd9", i)
    if j < 0:
        return None
    return data[i : j + 2]


def _rtsp_play_session(rtsp_url: str) -> tuple[socket.socket, str, str, bytes]:
    """OPTIONS/DESCRIBE/SETUP/PLAY; returns (sock, session, sdp_text, leftover)."""
    u = urlparse(rtsp_url)
    host = u.hostname or "127.0.0.1"
    port = u.port or 8554
    path = u.path or "/"
    base = f"rtsp://{host}:{port}{path}"

    sock = socket.create_connection((host, port), timeout=8)
    sock.settimeout(8)
    cseq = 1
    session = ""
    sdp = ""

    def req(method: str, url: str, extra: str = "", body: bytes = b"") -> tuple[int, str, bytes]:
        nonlocal cseq, session
        headers = [
            f"{method} {url} RTSP/1.0",
            f"CSeq: {cseq}",
            "User-Agent: deep-dog-rtsp-pull-verify",
        ]
        cseq += 1
        if session:
            headers.append(f"Session: {session}")
        if extra:
            headers.extend(extra.rstrip("\r\n").split("\r\n"))
        headers.append(f"Content-Length: {len(body)}")
        code, hdr, body_out = _rtsp_exchange(sock, "\r\n".join(headers) + "\r\n\r\n")
        if body:
            sock.sendall(body)
            # body already declared via Content-Length in rare cases; keep simple (no body senders)
        text = hdr.decode("latin1", errors="replace")
        for line in text.split("\r\n"):
            if line.lower().startswith("session:"):
                session = line.split(":", 1)[1].strip().split(";")[0].strip()
        return code, text, body_out

    code, _, _ = req("OPTIONS", base)
    if code >= 400:
        sock.close()
        raise ConnectionError(f"OPTIONS -> {code}")
    code, desc, sdp_b = req("DESCRIBE", base, "Accept: application/sdp")
    if code != 200:
        sock.close()
        raise ConnectionError(f"DESCRIBE -> {code}\n{desc}")
    sdp = sdp_b.decode("utf-8", errors="replace")
    setup_url = base.rstrip("/") + "/trackID=0"
    code, _, _ = req("SETUP", setup_url, "Transport: RTP/AVP/TCP;unicast;interleaved=0-1")
    if code >= 300:
        code, _, _ = req("SETUP", base, "Transport: RTP/AVP/TCP;unicast;interleaved=0-1")
    if code >= 300:
        sock.close()
        raise ConnectionError(f"SETUP failed {code}")
    code, _, rest = req("PLAY", base, "Range: npt=0.000-")
    if code >= 300:
        sock.close()
        raise ConnectionError(f"PLAY failed {code}")
    return sock, session, sdp, rest


def pull_h264_via_rtsp_tcp(rtsp_url: str, out_dir: Path, seconds: float) -> list[Path]:
    """PLAY H264/RTP over TCP interleaved; write Annex-B + decode frames via local ffmpeg."""
    sock, session, sdp, buf = _rtsp_play_session(rtsp_url)
    if "H264" not in sdp.upper() and "96" not in sdp:
        print(f"warn: SDP may not be H264:\n{sdp[:300]}")

    for old in list(out_dir.glob("frame_*.jpg")) + list(out_dir.glob("pull_5s.*")):
        old.unlink(missing_ok=True)

    annexb = bytearray()
    deadline = time.time() + max(1.0, seconds)
    try:
        while time.time() < deadline:
            try:
                chunk = sock.recv(65536)
            except socket.timeout:
                continue
            if not chunk:
                break
            buf += chunk
            while True:
                if len(buf) < 4:
                    break
                if buf[0] != 0x24:
                    idx = buf.find(b"$")
                    if idx < 0:
                        buf = b""
                        break
                    buf = buf[idx:]
                    continue
                length = struct.unpack("!H", buf[2:4])[0]
                if len(buf) < 4 + length:
                    break
                pkt = buf[4 : 4 + length]
                buf = buf[4 + length :]
                if len(pkt) < 12:
                    continue
                marker = bool(pkt[1] & 0x80)
                payload = pkt[12:]
                if not payload:
                    continue
                nal_type = payload[0] & 0x1F
                if nal_type == 28 and len(payload) >= 2:  # FU-A
                    fu_hdr = payload[1]
                    start = bool(fu_hdr & 0x80)
                    end = bool(fu_hdr & 0x40)
                    orig_type = fu_hdr & 0x1F
                    if start:
                        nal_hdr = (payload[0] & 0xE0) | orig_type
                        annexb += b"\x00\x00\x00\x01" + bytes([nal_hdr]) + payload[2:]
                    else:
                        annexb += payload[2:]
                    _ = end
                elif 1 <= nal_type <= 23:
                    annexb += b"\x00\x00\x00\x01" + payload
                # ignore STAP-A etc.
                _ = marker
    finally:
        try:
            if session:
                msg = (
                    f"TEARDOWN {rtsp_url} RTSP/1.0\r\nCSeq: 99\r\nSession: {session}\r\n"
                    f"Content-Length: 0\r\n\r\n"
                )
                sock.sendall(msg.encode())
        except Exception:
            pass
        sock.close()

    if len(annexb) < 64:
        raise ConnectionError(f"no H264 data received ({len(annexb)} bytes)")

    h264_path = out_dir / "pull_5s.h264"
    h264_path.write_bytes(bytes(annexb))
    ok: list[Path] = [h264_path]
    print(f"OK annex-b {h264_path} ({h264_path.stat().st_size} bytes)")

    if not shutil.which("ffmpeg"):
        return ok

    video_path = out_dir / "pull_5s.mp4"
    frame_pattern = str(out_dir / "frame_%03d.jpg")
    try:
        run_ffmpeg(
            [
                "ffmpeg",
                "-hide_banner",
                "-y",
                "-f",
                "h264",
                "-i",
                str(h264_path),
                "-an",
                "-c:v",
                "copy",
                str(video_path),
            ]
        )
        if video_path.is_file() and video_path.stat().st_size > 64:
            ok.append(video_path)
            print(f"OK mp4 {video_path} ({video_path.stat().st_size} bytes)")
    except Exception as e:
        print(f"warn: remux h264->mp4 failed: {e}")

    try:
        for old in out_dir.glob("frame_*.jpg"):
            old.unlink(missing_ok=True)
        run_ffmpeg(
            [
                "ffmpeg",
                "-hide_banner",
                "-y",
                "-f",
                "h264",
                "-i",
                str(h264_path),
                "-an",
                "-frames:v",
                "30",
                "-q:v",
                "3",
                frame_pattern,
            ]
        )
        frames = sorted(out_dir.glob("frame_*.jpg"))
        if frames and frames[0].read_bytes()[:2] == b"\xff\xd8":
            ok.extend(frames)
            print(f"OK frames {len(frames)} (e.g. {frames[0]} {frames[0].stat().st_size} bytes)")
        else:
            print("warn: no decoded JPEG frames from H264")
    except Exception as e:
        print(f"warn: H264 frame extract failed: {e}")

    if len(ok) < 2 and not any(p.suffix == ".jpg" for p in ok):
        # hard gate: at least annex-b is enough if non-trivial; prefer frames
        if h264_path.stat().st_size < 256:
            raise ConnectionError("H264 bitstream too small")
    return ok


def pull_jpeg_via_rtsp_tcp(rtsp_url: str, out_dir: Path, seconds: float) -> list[Path]:
    """Minimal PLAY client for JPEG/RTP over TCP interleaved (RFC 2435, fragmented)."""
    sock, session, sdp, buf = _rtsp_play_session(rtsp_url)
    base = rtsp_url
    _ = sdp

    try:
        for old in out_dir.glob("frame_*.jpg"):
            old.unlink(missing_ok=True)

        deadline = time.time() + max(1.0, seconds)
        frames: list[Path] = []
        # frag_offset -> payload bytes for current frame
        parts: dict[int, bytes] = {}
        while time.time() < deadline and len(frames) < 200:
            try:
                chunk = sock.recv(65536)
            except socket.timeout:
                continue
            if not chunk:
                break
            buf += chunk
            while True:
                if len(buf) < 4:
                    break
                if buf[0] != 0x24:
                    idx = buf.find(b"$")
                    if idx < 0:
                        buf = b""
                        break
                    buf = buf[idx:]
                    continue
                length = struct.unpack("!H", buf[2:4])[0]
                if len(buf) < 4 + length:
                    break
                pkt = buf[4 : 4 + length]
                buf = buf[4 + length :]
                if len(pkt) < 12 + 8:
                    continue
                marker = bool(pkt[1] & 0x80)
                jpeg_hdr = pkt[12:20]
                frag_off = (jpeg_hdr[1] << 16) | (jpeg_hdr[2] << 8) | jpeg_hdr[3]
                q = jpeg_hdr[5]
                payload = pkt[20:]
                if frag_off == 0 and q == 255 and len(payload) >= 4:
                    pass
                parts[frag_off] = payload
                if not marker:
                    continue
                ordered = b"".join(parts[k] for k in sorted(parts))
                parts.clear()
                jpeg = _jpeg_from_rfc2435_payload(ordered)
                if not jpeg:
                    continue
                path_out = out_dir / f"frame_{len(frames) + 1:03d}.jpg"
                path_out.write_bytes(jpeg)
                frames.append(path_out)
        if not frames:
            raise ConnectionError("no JPEG frames received over RTSP TCP")
        return frames
    finally:
        try:
            if session:
                msg = (
                    f"TEARDOWN {base} RTSP/1.0\r\nCSeq: 99\r\nSession: {session}\r\n"
                    f"Content-Length: 0\r\n\r\n"
                )
                sock.sendall(msg.encode())
        except Exception:
            pass
        sock.close()


def try_ffmpeg_pull(rtsp: str, out_dir: Path, dur: float, skip_video: bool, skip_frames: bool) -> list[Path]:
    ok: list[Path] = []
    video_path = out_dir / "pull_5s.mp4"
    frame_pattern = str(out_dir / "frame_%03d.jpg")
    common_in = [
        "ffmpeg",
        "-hide_banner",
        "-y",
        "-rtsp_transport",
        "tcp",
        "-rw_timeout",
        "5000000",
        "-i",
        rtsp,
        "-t",
        f"{dur:.1f}",
    ]
    if not skip_video:
        run_ffmpeg(
            common_in
            + ["-an", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-preset", "ultrafast", str(video_path)]
        )
        if not video_path.is_file() or video_path.stat().st_size < 1024:
            raise RuntimeError(f"video missing or too small: {video_path}")
        ok.append(video_path)
        print(f"OK video {video_path} ({video_path.stat().st_size} bytes)")
    if not skip_frames:
        for old in out_dir.glob("frame_*.jpg"):
            old.unlink(missing_ok=True)
        run_ffmpeg(common_in + ["-an", "-q:v", "3", frame_pattern])
        frames = sorted(out_dir.glob("frame_*.jpg"))
        if not frames:
            raise RuntimeError("no frame_*.jpg produced")
        if frames[0].read_bytes()[:2] != b"\xff\xd8":
            raise RuntimeError(f"frame does not look like JPEG: {frames[0]}")
        ok.extend(frames)
        print(f"OK frames {len(frames)} (e.g. {frames[0]} {frames[0].stat().st_size} bytes)")
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description="Verify deep-dog MediaMTX RTSP pull")
    ap.add_argument("--device", default=DEFAULT_DEVICE, help="device HTTP base URL")
    ap.add_argument("--rtsp", default=DEFAULT_RTSP, help="MediaMTX RTSP URL")
    ap.add_argument("--outdir", default="", help="subdir under fixtures/rtsp_pull_verify")
    ap.add_argument("--seconds", type=float, default=5.0, help="capture duration")
    ap.add_argument("--no-enable-push", action="store_true", help="skip POST vision_publish")
    ap.add_argument("--skip-video", action="store_true", help="only extract frames")
    ap.add_argument("--skip-frames", action="store_true", help="only write video file")
    ap.add_argument("--python-only", action="store_true", help="skip ffmpeg, use TCP JPEG puller")
    args = ap.parse_args()

    sub = args.outdir.strip() or "jpeg"
    out_dir = DEFAULT_OUT / sub
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"output: {out_dir}")

    if not args.no_enable_push:
        print(f"enable push on {args.device} ...")
        st = ensure_push(args.device)
        print(f"device ready: {json.dumps(st, ensure_ascii=False)}")
    else:
        try:
            st = http_json(f"{args.device.rstrip('/')}/api/status")
            print(f"device status: {json.dumps(st, ensure_ascii=False)}")
        except urllib.error.URLError as e:
            print(f"warn: cannot read device status: {e}")

    dur = max(1.0, float(args.seconds))
    ok_paths: list[Path] = []
    ffmpeg_err: Exception | None = None

    if not args.python_only and shutil.which("ffmpeg"):
        try:
            ok_paths = try_ffmpeg_pull(args.rtsp, out_dir, dur, args.skip_video, args.skip_frames)
        except Exception as e:
            ffmpeg_err = e
            print(f"ffmpeg path failed: {e}; trying Python RTSP TCP JPEG puller ...")
    else:
        print("using Python RTSP TCP puller")

    if not ok_paths:
        # Prefer codec inferred from outdir name / DESCRIBE
        prefer_h264 = sub.lower().startswith("h264")
        if prefer_h264:
            print("using Python RTSP TCP H264 puller")
            ok_paths = pull_h264_via_rtsp_tcp(args.rtsp, out_dir, dur)
        else:
            print("using Python RTSP TCP JPEG puller")
            frames = pull_jpeg_via_rtsp_tcp(args.rtsp, out_dir, dur)
            ok_paths.extend(frames)
            print(f"OK python-tcp frames {len(frames)} (e.g. {frames[0]} {frames[0].stat().st_size} bytes)")
            if shutil.which("ffmpeg") and not args.skip_video:
                video_path = out_dir / "pull_5s.mp4"
                try:
                    run_ffmpeg(
                        [
                            "ffmpeg",
                            "-hide_banner",
                            "-y",
                            "-framerate",
                            "5",
                            "-i",
                            str(out_dir / "frame_%03d.jpg"),
                            "-c:v",
                            "libx264",
                            "-pix_fmt",
                            "yuv420p",
                            "-t",
                            f"{dur:.1f}",
                            str(video_path),
                        ]
                    )
                    if video_path.is_file() and video_path.stat().st_size > 512:
                        ok_paths.insert(0, video_path)
                        print(f"OK video-from-frames {video_path} ({video_path.stat().st_size} bytes)")
                except Exception as e:
                    print(f"warn: could not mux frames to mp4: {e}")

    if not ok_paths:
        raise SystemExit(f"ACCEPT FAIL: no outputs; ffmpeg_err={ffmpeg_err}")

    print("ACCEPT: RTSP pull verify passed")
    for p in ok_paths[:8]:
        print(f"  - {p}")
    if len(ok_paths) > 8:
        print(f"  ... and {len(ok_paths) - 8} more")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
