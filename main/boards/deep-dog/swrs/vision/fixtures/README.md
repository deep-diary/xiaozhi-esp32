# 视觉联调测试图（fixtures）

| 文件 | 人物 | 用途 |
|------|------|------|
| [`ge_weidong.png`](./ge_weidong.png) | **葛维冬** | Immich / 设备 S05 真名验收（单人正脸） |
| [`couple_selfie.jpg`](./couple_selfie.jpg) | 双人自拍（含葛维冬） | 多脸场景；设备侧仍只处理最高分一张 |
| [`camera_view_couple.png`](./camera_view_couple.png) | 设备 240 预览截图（双人） | 2026-07-19 网页画面另存 |
| [`web_screenshot_ge_weidong_ui.png`](./web_screenshot_ge_weidong_ui.png) | 控制页整页截图（#2） | 含 UI；验证 Immich 时建议裁预览区 |

**禁止**把 Immich API Key 写进本目录或仓库。主机侧探针可用 DeepWeb `config.json` 的局域网 Key；设备侧用 `POST /api/immich_config` 写入 NVS。
