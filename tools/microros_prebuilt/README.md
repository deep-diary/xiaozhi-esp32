# micro-ROS 预编译库（deep-dog CE01）

仓库内 vendored 的 **`libmicroros.a` + ROS 头文件**，供 **Windows**（及任意环境）跳过 `libmicroros.mk` 长编库。

> 组件**源码骨架**仍由 Component Manager 拉到 `managed_components/`（git 不跟踪）。本目录只放交叉编译产物。

## 为何需要

| 环境 | 编 `libmicroros.a` |
|------|-------------------|
| **macOS / Linux** | 原生 `make -f libmicroros.mk` 即可 |
| **Windows 原生** | 常失败（缺 GNU make、CRLF、`/mnt/d` 路径） |
| **Windows + WSL** | 官方补丁路径，仍慢、路径易错 |
| **任意 OS + 本目录** | `install_microros_prebuilt.py` 复制产物 → **跳过编库** |

「Win 缺 `.a` 时走 WSL」= **在 Windows 上若没预编译包，只能借 Linux 环境编库**；不是说你必须在 Linux 上开发整机固件。

## 当前 profile

| 目录 | 说明 |
|------|------|
| [`esp32s3-idf5.5-humble22/`](./esp32s3-idf5.5-humble22/) | ESP-IDF **5.5.2** · target **esp32s3** · micro-ROS component **22.0.0** (Humble) |

详见各目录内 `MANIFEST.json`（含 `app-colcon.meta` sha256）。

## Windows Agent 流程（推荐）

```powershell
# 0. 加载 ESP-IDF v5.5.x（勿 source ROS 2）
# 1. clone 后已有 tools/microros_prebuilt/

idf.py set-target esp32s3

# 2. 拉组件骨架（无 .a）
idf.py add-dependency "micro-ros/micro_ros_espidf_component>=22.0.0,<23.0.0"

# 3. 安装预编译库
python scripts/deep_dog/install_microros_prebuilt.py

# 4. Windows CMake 补丁（跳过原生 make / 可选跳过 WSL 编库）
python scripts/deep_dog/patch_microros_windows_cmake.py

# 5. 编固件
idf.py -DDEEP_DOG_MICROROS=ON build
```

成功标志：配置/链接阶段出现 **`[deep-dog] using existing libmicroros.a`**，且**不会**长时间跑 `libmicroros.mk` 或 WSL 编库脚本。

## macOS / Linux

同样可先 `deep_dog_fetch_microros.sh`，再：

```bash
python3 scripts/deep_dog/install_microros_prebuilt.py
idf.py -DDEEP_DOG_MICROROS=ON build
```

也可不安装、直接本机 `make -f libmicroros.mk` 重编。

## 更新预编译包（维护者 · Mac/Linux）

改 `app-colcon.meta` 或升级 micro-ROS 组件后必须重做：

```bash
./scripts/deep_dog/deep_dog_fetch_microros.sh
idf.py set-target esp32s3
idf.py -DDEEP_DOG_MICROROS=ON build   # 本机编出 libmicroros.a

PROFILE=esp32s3-idf5.5-humble22
SRC=managed_components/micro-ros__micro_ros_espidf_component
DST=tools/microros_prebuilt/$PROFILE
cp -a "$SRC/libmicroros.a" "$SRC/include" "$SRC/include_override" "$DST/"
# 更新 $DST/MANIFEST.json（idf 版本、component commit、app_colcon_meta_sha256）
python3 scripts/deep_dog/install_microros_prebuilt.py --force  # 自测
```

需求出处：[CE01](../../main/boards/deep-dog/swrs/cloud_edge/CE01-microros-link-smoke.md) · [microros/README](../../main/boards/deep-dog/microros/README.md)
