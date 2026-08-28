# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

规则文件（与 `.cursor/rules/` 对应，始终加载）：

@.claude/rules/deep-dog-board.md
@.claude/rules/deep-dog-swrs-first.md

## Project Overview

XiaoZhi (小智) AI chatbot — an ESP-IDF firmware for a voice-interaction terminal (streaming ASR + LLM + TTS) with device control via the MCP protocol. Supports 70+ boards on ESP32-S3 (primary), C3/C5/C6/P4. This fork's primary custom board is **deep-dog** (`main/boards/deep-dog/`), a robot dog on ESP32-S3 + 16MB flash. `AGENTS.md` and most docs/comments are in Chinese; this repo is developed on Windows (with WSL used for micro-ROS builds).

## Build Commands

Requires ESP-IDF v5.5.x (`export.sh` / `export.bat` loaded so `idf.py` is on PATH).

```bash
idf.py set-target esp32s3        # once, or when switching chips
idf.py build
idf.py -p <PORT> flash monitor   # flash + serial monitor
idf.py monitor                   # monitor only
idf.py menuconfig                # board type is chosen here: "Xiaozhi Assistant" menu
idf.py fullclean && idf.py build # clean rebuild
```

There is no unit-test suite; verification is build + flash + watching the boot log (check for `ESP_LOGE`, crash/panic, Wi-Fi/MQTT failures, OOM warnings, module init messages). `idf.py monitor` streams forever — observe ~10–15 s of boot log, then stop and analyze.

### Building deep-dog (recommended, matches CI/release)

```bash
idf.py set-target esp32s3
python scripts/release.py deep-dog   # applies config.json sdkconfig_append, builds, packages
```

Or manually: append `CONFIG_BOARD_TYPE_DEEP_DOG=y` plus the lines from `main/boards/deep-dog/config.json` → `builds[0].sdkconfig_append` to `sdkconfig` (including the custom partition table `partitions/v2/16m_deep_dog.csv`), then `idf.py build`. When switching to/from the deep-dog partition table, do a full `idf.py erase-flash flash`.

Optional deep-dog CMake feature flags (default OFF, defined in root `CMakeLists.txt`):
- `-DDEEP_DOG_HANDLE_BT=ON` — Bluepad32/BTstack Xbox BLE gamepad host (fetch via `scripts/deep_dog/deep_dog_fetch_bluepad32.sh` first)
- `-DDEEP_DOG_MICROROS=ON` — micro-ROS client (heavy first build; see `main/boards/deep-dog/microros/README.md`)

Helper/verify scripts live in `scripts/` and `scripts/deep_dog/` (e.g. `deep_dog_mqtt_verify.py`, RTSP/IMU/face verification, WSL micro-ROS toolchain scripts).

## Architecture

### Core application (`main/`)

- `application.cc/h` — `Application` singleton: main event loop, coordinates protocol ↔ audio ↔ display, OTA checks.
- `device_state_machine.cc` + `device_state.h` — device states (idle/connecting/listening/speaking...). See `docs/architecture-*.md` for diagrams.
- `protocols/` — `Protocol` abstract base with two implementations: `mqtt_protocol.cc` (MQTT+UDP) and `websocket_protocol.cc`. Server protocol docs: `docs/mqtt-udp.md`, `docs/websocket.md`.
- `audio/` — `AudioService` with OPUS codec, codec drivers (`codecs/` — ES8311/8374/8388/8389, box, no-audio), audio engines selected per chip in `main/CMakeLists.txt` (AFE engine + custom wake word on S3/P4, lite engine + esp_wake_word elsewhere).
- `mcp_server.cc` — device-side MCP server exposing device-control tools to the LLM (`docs/mcp-protocol.md`, `docs/mcp-usage.md`).
- `display/` — `Display` base → OLED / LCD (LVGL) / emote display variants.
- `ota.cc`, `settings.cc`, `assets.cc` — OTA, NVS settings, assets partition (v2 partition layout in `partitions/v2/`, incompatible with v1).

### Board abstraction & selection

- `main/boards/common/board.h` defines the `Board` base class (subclassed by `WifiBoard`, `Ml307Board`, `DualNetworkBoard`, etc.). Each board implements a subclass and registers it with the `DECLARE_BOARD(ClassName)` macro.
- Board choice is a Kconfig option (`CONFIG_BOARD_TYPE_*` in `main/Kconfig.projbuild`). `main/CMakeLists.txt` maps it to a `BOARD_TYPE` directory and **globs all `.c/.cc/.cpp` sources recursively** under `main/boards/<board>/` (excluding `ref/` and deep-dog `handle/third_party/`), and sets per-board fonts/emoji collections.
- Each board dir has `config.h` (pins/hardware) and `config.json` (target chip + `sdkconfig_append` used by `scripts/release.py` and CI). Guide: `docs/custom-board.md`.

### deep-dog board (`main/boards/deep-dog/`)

MQTT is the core protocol for its feature modules (handle, CAN, motor, servo/gimbal, LED, IMU, touch, vision/RTSP, face recognition, pairing, HTTP server...). Key files: `config.h` (GPIO + `DEEP_DOG_EXT_PIN_MODE` paired ext-pin modes: CAN/UART/RS485/PWM/IO/AD/LED), `board_features.h` (layered feature ENABLE macros), `FEATURE_FLAGS.md` (profile table). Modules must `#include "config.h"`, never `board_features.h` directly. Requirements docs live in `swrs/` (see below).

## Project Rules (from `.cursor/rules/` and `AGENTS.md` — follow these)

1. **Board isolation**: develop deep-dog features inside `main/boards/deep-dog/` only. Modifying shared code (`main/boards/common/`, `main/` root, components) is allowed only when verified impossible to avoid, with a minimal diff and a comment at the change site:
   ```c
   // [deep-dog] 合入说明: <one-line reason>
   // 背景: <why a board-level solution isn't possible>
   ```
2. **SWRS first**: before implementing a new deep-dog feature/change, search `main/boards/deep-dog/swrs/` (index: `swrs/README.md`, order: `swrs/ROADMAP.md`) for a matching requirement doc. If none exists, create/update the doc first (following existing subdomain numbering, e.g. `mqtt/modules/NN-name.md`), then code. Cite the requirement doc path(s) when implementing. MQTT contract changes must also update `swrs/mqtt/protocol/deep-dog-mqtt.yml`.
3. **Frontend/backend sync**: changes to deep-dog MQTT contracts, capabilities, ext-pin modes, or pairing must be synced to the deep-trace requirements repo (separate workspace; see `.cursor/rules/deep-dog-sync-frontend-reqs.mdc` for paths). Stable `device_id` = lowercase STA MAC without colons; user pairing flow uses a 6-digit pairing code.
4. After code changes, the expected workflow is: build → flash → monitor boot log → fix any errors → repeat, then report.

## Conventions

- C++17, Google C++ style (`docs/code_style.md`); logs via `ESP_LOGI`/`ESP_LOGE` IDF macros with a `TAG`.
- Configuration goes in `sdkconfig.defaults*` / Kconfig / board `config.json`, not hardcoded.
- Module tunables (baud rates, motor gains, gaits) belong in each module's `*_config.h`; never hardcode GPIO numbers inside modules.
- `PROJECT_VER` is set in the root `CMakeLists.txt` (currently 2.3.0, ESP-IDF v5.5.x line).
