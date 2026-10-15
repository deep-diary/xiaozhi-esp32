# LL-003 · ccache 导致 CMake 配置失败（CXX 特性探测）

## LL-003 ccache 启用时 CMake 报 "target_compile_features no known features for CXX compiler"

- **现象**：`idf.py reconfigure` / `idf.py build` 在配置阶段失败：

  ```
  CMake Error at managed_components/esphome__esp-hub75/CMakeLists.txt:102 (target_compile_features):
    target_compile_features no known features for CXX compiler
    "GNU"
    version 14.2.0.
  -- Configuring incomplete, errors occurred!
  ```

  注意报错点在**第三方组件**（esp-hub75），容易误判为组件本身不兼容 GCC 14。

- **根因**：本机 `idf.py` 默认带 `-DCCACHE_ENABLE=1`，ccache 启动器使 CMake 的
  C++ 编译器特性探测（`CMAKE_CXX_COMPILE_FEATURES`）为空，于是任何
  `target_compile_features(... cxx_std_20)` 都失败。

- **正确做法**：本次编译禁用 ccache，重新配置即可通过：

  ```powershell
  $env:IDF_PATH='D:\working\dev\esp-idf'; $env:IDF_CCACHE_ENABLE='0'; . "$env:IDF_PATH\export.ps1"
  cd D:\working\test-platform\xiaozhi-esp32
  idf.py reconfigure
  idf.py build
  ```

- **相关文件**：`managed_components/esphome__esp-hub75/CMakeLists.txt`（报错点，勿改）、`build/CMakeCache.txt`

## LL-003b 新增板级 `.cc` 后必须重新 configure（GLOB 缓存）

- **现象**：新增 `main/boards/deep-dog/**/*.cc` 后直接 `idf.py build`，
  链接期报 `undefined reference to ...`（新文件里的符号）。
- **根因**：`main/CMakeLists.txt` 用 `file(GLOB_RECURSE ...)` 收集板级源码，
  glob 结果在 **configure 时**固化；增量 build 不会重新 glob。
- **正确做法**：新增/删除板级源文件后先 `idf.py reconfigure`（或 `idf.py build` 前删除 build 缓存），再 build。
- **相关文件**：`main/CMakeLists.txt:898-909`
