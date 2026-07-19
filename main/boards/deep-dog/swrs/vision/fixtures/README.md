# 视觉联调测试图（fixtures）

| 文件 | 人物 | 用途 |
|------|------|------|
| [`ge_weidong.png`](./ge_weidong.png) | **葛维冬** | Immich / 设备真名验收（单人正脸；主机清晰图可认） |
| [`ge_weidong_long240.jpg`](./ge_weidong_long240.jpg) | 葛维冬（长边 240） | 负例：主机 Immich `people=[]` |
| [`ge_weidong_short480.jpg`](./ge_weidong_short480.jpg) | 葛维冬（短边 480） | 主机 Immich 对照正例 |
| [`ge_weidong_640x480.jpg`](./ge_weidong_640x480.jpg) | 葛维冬（中心裁 640×480） | 贴近设备帧尺寸的主机对照 |
| [`couple_selfie.jpg`](./couple_selfie.jpg) | 双人自拍（含葛维冬） | 多脸场景 |
| [`camera_view_couple.png`](./camera_view_couple.png) | 旧设备 **240×240** 预览截图（双人） | S06 对照：低像素 Immich 易失败 |
| [`web_screenshot_ge_weidong_ui.png`](./web_screenshot_ge_weidong_ui.png) | 控制页整页截图（#2） | 含 UI；验证 Immich 时建议裁预览区 |

S06 起设备预览/检测实选 **640×480**（见 [S06](../server/S06-higher-resolution.md)）。

**禁止**把 Immich API Key 写进本目录或仓库。主机侧探针可用 DeepWeb `config.json` 的局域网 Key；设备侧用 `POST /api/immich_config` 写入 NVS。
