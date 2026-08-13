#pragma once

#include <esp_err.h>

/** internal 栈 worker：NVS meta 写入（供 PSRAM 栈上的 face/immich task 委托）。 */
bool DeepDogFacePersistInit();
void DeepDogFacePersistShutdown();
bool DeepDogFacePersistIsReady();

/** 异步排队一次 SaveMeta（可合并）。 */
void DeepDogFacePersistFlushAsync();

/** 阻塞直到 persist worker 完成写入。 */
bool DeepDogFacePersistFlushSync();

/** 仅 persist worker / 启动路径（internal 栈）调用。 */
esp_err_t DeepDogFaceRecognizeSaveMetaToNvs();
