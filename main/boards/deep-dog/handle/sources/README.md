# handle BT 源（Bluepad32 + BTstack）

板载 Xbox BLE：`HandleBt*` → Normalize → `HandleEventHub`（`source=bt`）。  
规格：[I02](../../swrs/input/I02-source-bluepad32-xbox.md)。

## 默认

`DEEP_DOG_HANDLE_BT_ENABLE=0`（[`handle_config.h`](../handle_config.h)）。  
默认固件**不**链接 Bluepad32；`pair` / `rumble` 为桩日志。

## 启用步骤

1. 拉取依赖（生成 `handle/third_party/bluepad32`，已 gitignore）：

```bash
./scripts/deep_dog_fetch_bluepad32.sh
```

2. 配置编译（根目录 CMake option）：

```bash
idf.py -DDEEP_DOG_HANDLE_BT=ON reconfigure build
```

这会：

- 把 Bluepad32/BTstack 加入 `EXTRA_COMPONENT_DIRS`
- 给 `main` 定义 `DEEP_DOG_HANDLE_BT_ENABLE=1`
- `PRIV_REQUIRES`：`bluepad32` `btstack`

3. menuconfig（首次启用时核对）：

- Bluepad32 Platform = **Custom**
- Bluetooth Controller 按 Bluepad32 ESP32-S3 文档（BLE）
- **关闭**小智 BluFi 配网 BT（与 BTstack Host 互斥）

4. 配对：手柄 Xbox 键开机 → 长按配对键 3s → MQTT `handle/cmd` `{"action":"pair"}` 或上电自动扫描。

## 授权

- Bluepad32：Apache-2.0  
- BTstack：开源免费；**闭源商用需单独授权**
