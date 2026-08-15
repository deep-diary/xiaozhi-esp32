# vision/server

设备作为 **HTTP 服务器**（局域网 :8080）。

| ID | 文档 | 状态 |
|----|------|------|
| V-S01 | [S01-http-dog-motion.md](./S01-http-dog-motion.md) | 主体 ✅ |
| V-S02 | [S02-http-mjpeg.md](./S02-http-mjpeg.md) | ✅ |
| V-S03 | [S03-http-face-overlay.md](./S03-http-face-overlay.md) | 一期 ✅ |
| V-S04 | [S04-local-face-numeric-id.md](./S04-local-face-numeric-id.md) | ✅ |
| V-S05 | [S05-immich-real-name.md](./S05-immich-real-name.md) | ✅ |
| V-S06 | [S06-higher-resolution.md](./S06-higher-resolution.md) | §9 双分辨率 **评估 ✅ / 待实现** |
| V-S07 | [S07-face-control-mcp.md](./S07-face-control-mcp.md) | 本轮 |
| V-S08 | [S08-rtsp-face-wdt-mitigation.md](./S08-rtsp-face-wdt-mitigation.md) | 已落地 |
| **V-S09** | [S09-internal-sram-optimization.md](./S09-internal-sram-optimization.md) | **评估文档 ✅ / 待逐项落地** |

顺序：S01 → S02 → … → S06；运维与并发见 S08；**internal SRAM 优化见 S09**。总表见 [ROADMAP](../../ROADMAP.md)。

> **S06 §9（下一增量）**：OV2640 + 8MB PSRAM 上 **640×480 采集 + 320×240 RTSP 推流降级** 评估为工程可行，详见 [S06 §9](./S06-higher-resolution.md)。当前联调默认仍为 240²。历史 OV3660 全 VGA 同分辨率路径见 S06 §8.2。
