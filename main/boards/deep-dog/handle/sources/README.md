# handle BT 源（Bluepad32 + BTstack）

板载 Xbox BLE：`HandleBt*` → Normalize → `HandleEventHub`（`source=bt`）。  
规格：[I02](../../swrs/input/I02-source-bluepad32-xbox.md)。

## 日常剖面（推荐）

| 项 | 选择 |
|----|------|
| 板载 BT | **关**（`-DDEEP_DOG_HANDLE_BT=OFF`，CMake 默认即 OFF） |
| 手柄输入 | **PC 桥** `source=wifi`（[I03](../../swrs/input/I03-source-pc-mqtt-bridge.md)） |
| 灯/震调试 | `handle/cmd` **`action: output`**（DS4 HID 或 Xbox pygame rumble，见 [I09](../../swrs/input/I09-ds4-output-feedback.md)） |

板载 BT 已在实机验证过；日常关是为给人脸 / 板级 MQTT / 唤醒让出 **INTERNAL** 堆（开 BT 时常见 `Error create mqtt task` / `esp-aes`）。

关 BT 重建：

```bash
idf.py -DDEEP_DOG_HANDLE_BT=OFF reconfigure build flash
# 若 sdkconfig 仍残留 CONFIG_BT_ENABLED=y，一并关掉后再 build
```

串口应出现 `BT source disabled`（或无 Bluepad32 banner），且 **无** `BTstack up and running`。

PC 桥示例：

```bash
# DS4（默认 HID + 灯震）
python3 scripts/deep_dog/deep_dog_handle_bridge.py --via lan

# Xbox（pygame + output 震动；macOS 优先蓝牙连手柄）
python3 scripts/deep_dog/deep_dog_handle_bridge.py --via lan --layout xbox
python3 scripts/deep_dog/deep_dog_handle_bridge.py --probe-xbox-rumble
```

## 默认宏

`DEEP_DOG_HANDLE_BT_ENABLE=0`（[`handle_config.h`](../handle_config.h)）。  
默认固件**不**链接 Bluepad32；`pair` / `rumble` 为桩日志。

## 瘦剖面再开 BT（可选）

适用：不做 Face AI、可接受云端 OTA/板级 MQTT 压力偏大时的运控联调。

1. 拉取依赖（生成 `handle/third_party/bluepad32`，已 gitignore）：

```bash
./scripts/deep_dog/deep_dog_fetch_bluepad32.sh
```

2. 配置编译：

```bash
idf.py -DDEEP_DOG_HANDLE_BT=ON reconfigure build
```

这会：

- 把 Bluepad32/BTstack 加入 `EXTRA_COMPONENT_DIRS`（并建议在 `components/` 建同名 symlink）
- **关闭 `MINIMAL_BUILD`**
- 给 `main` 定义 `DEEP_DOG_HANDLE_BT_ENABLE=1` 并链 `bluepad32` / `btstack`
- sdkconfig：`CONFIG_BT_ENABLED`、`CONFIG_BLUEPAD32_PLATFORM_CUSTOM`、`CONFIG_BT_CONTROLLER_ONLY`、关 BluFi

选项写入 `build/CMakeCache.txt`。`fullclean` / 删 `build/` 后须**再带** `-DDEEP_DOG_HANDLE_BT=ON`。

### 烧录与串口

- **先停掉**正在跑的 `idf.py monitor`，再 `flash`。
- 习惯：`idf.py build flash monitor`。

首次拉取后若尚无 symlink：

```bash
BP=main/boards/deep-dog/handle/third_party/bluepad32/src/components
ln -sfn "../$BP/bluepad32" components/bluepad32
ln -sfn "../$BP/btstack" components/btstack
ln -sfn "../$BP/cmd_nvs" components/cmd_nvs
ln -sfn "../$BP/cmd_system" components/cmd_system
```

3. menuconfig（首次启用时核对）：Bluepad32 Platform = **Custom**；关 BluFi。

4. 配对：Xbox 键开机 → 长按配对 3s → `handle/cmd` `{"action":"pair"}`。

### 启动时序（开 BT 时）

`HandleBtStart` **先于** WiFi（HCI 要大块 DMA）。AFE INTERNAL 栈失败会回退 PSRAM（`afe_audio_engine.cc`）。Face AI 延后试启。开 BT 时云端 OTA/TLS / 板级 MQTT 仍可能因 INTERNAL 不足失败。

## 授权

- Bluepad32：Apache-2.0  
- BTstack：开源免费；**闭源商用需单独授权**
