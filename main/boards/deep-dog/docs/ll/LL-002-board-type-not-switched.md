# LL-002 板型未切换（sdkconfig `BOARD_TYPE`）导致编出别的板

记录时间：2026-10-14
场景：deep-dog 固件联调初期，`idf.py build` 成功、`idf.py flash` 成功，但设备启动成了别的板。

---

## LL-002-1 编译通过却跑成 `bread-compact-wifi`

- **现象**：
  `idf.py build`/`flash` 均成功，但串口启动日志显示：
  ```
  I (228) Board: UUID=... SKU=bread-compact-wifi
  I (228) CompactWifiBoard: Install SSD1306 driver
  E (238) lcd_panel.io.i2c: panel_io_i2c_tx_buffer(193): i2c transaction failed
  ```
  完全看不到任何 `deep_dog_*` / `dog_mqtt_*` 日志；且 `deep-dog` 目录下的源码（含新增文件）**根本没被编译**。

- **根因**：
  板型由 `main/CMakeLists.txt` 以 **`CONFIG_BOARD_TYPE_*`** 决定（`elseif(CONFIG_BOARD_TYPE_DEEP_DOG) → set(BOARD_TYPE "deep-dog")`），随后用 `BOARD_TYPE` 去 glob `boards/${MANUFACTURER}/${BOARD_TYPE}/**`。
  当时 `sdkconfig` 是 `CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI=y`（`DEEP_DOG is not set`），于是 `BOARD_TYPE=bread-compact-wifi`，**deep-dog 目录被整体排除**——编译不报错，只是编了错的板。

- **正确做法**：
  切换板型必须落到板级 `config.json` 的官方流程，或显式改 Kconfig choice（**同时**把其余板置 not set）：
  ```bash
  # 推荐：官方流程（内部按 config.json 重置 sdkconfig + append 板级依赖）
  rm -f sdkconfig sdkconfig.old && rm -rf build
  python scripts/release.py deep-dog
  ```
  手工切换时，除板型 choice 外，还要同步 deep-dog 的板级依赖（来自 `main/boards/deep-dog/config.json` 的 `sdkconfig_append`）：
  - 分区表：`CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/v2/16m_deep_dog.csv"`
  - 摄像头：`CONFIG_CAMERA_OV3660=y`
  - 人脸特征模型落分区：`CONFIG_HUMAN_FACE_FEAT_MODEL_IN_FLASH_PARTITION=y`

- **验证**：
  ```bash
  # 1) 编译配置确为 deep-dog
  grep -E "CONFIG_BOARD_TYPE_DEEP_DOG|16m_deep_dog" sdkconfig
  # 2) 启动日志出现 deep-dog 特征
  #    I (...) Board: ... SKU=deep-dog
  #    I (...) dog_mqtt: board MQTT started ...
  ```
  经验判据：**只要启动日志没有 `SKU=deep-dog` 和 `dog_mqtt*`，就是编错了板**（编译成功 ≠ 编对了板）。

- **相关文件**：`main/CMakeLists.txt`（`CONFIG_BOARD_TYPE_DEEP_DOG` → `BOARD_TYPE` 分支）、`main/boards/deep-dog/config.json`、`sdkconfig`、`scripts/release.py`
