#ifndef _DEEP_DOG_FACE_AI_CONFIG_H_
#define _DEEP_DOG_FACE_AI_CONFIG_H_

#include "config.h"

/**
 * 网页人脸检测（human_face_detect / ESP-DL）相关配置（从 board `config.h` 拆分）。
 *
 * 联调模式（当前默认）：检测约 1s 一次、Immich 失败退避 15s；分辨率见 board config.json
 *（240×240 降内存 / 640×480 提 Immich 成功率，二选一）。
 */

/** 1=编译进固件；0=桩实现；默认见 board_features.h */
#ifndef DEEP_DOG_FACE_AI_ENABLE
#define DEEP_DOG_FACE_AI_ENABLE 1
#endif

/** 开机运行时总开关（检测+识别）；0=关，运行中由 MQTT face/cmd / HTTP 再开 */
#ifndef DEEP_DOG_FACE_AI_DEFAULT_ENABLED
#define DEEP_DOG_FACE_AI_DEFAULT_ENABLED 0
#endif

/**
 * 默认送帧最小间隔（ms）；运行时可由 MQTT face/cmd.detect_interval_ms 覆盖。
 * 对齐推流约 3fps 可用 333；默认 500 降低与 H264 叠加时 esp_timer/CPU 压力。
 */
#ifndef DEEP_DOG_FACE_AI_MIN_INTERVAL_MS
#define DEEP_DOG_FACE_AI_MIN_INTERVAL_MS 500
#endif

#ifndef DEEP_DOG_FACE_AI_INTERVAL_MIN_MS
#define DEEP_DOG_FACE_AI_INTERVAL_MIN_MS 200
#endif
#ifndef DEEP_DOG_FACE_AI_INTERVAL_MAX_MS
#define DEEP_DOG_FACE_AI_INTERVAL_MAX_MS 5000
#endif

/** live 模式下识别最小间隔（ms）；identity 模式与检测同间隔 */
#ifndef DEEP_DOG_FACE_RECOG_MIN_INTERVAL_MS
#define DEEP_DOG_FACE_RECOG_MIN_INTERVAL_MS 2000
#endif

/** 1=RTSP 推流时仍做人脸；0=推流时跳过送帧 */
#ifndef DEEP_DOG_FACE_AI_DURING_RTSP
#define DEEP_DOG_FACE_AI_DURING_RTSP 1
#endif

/** RTSP/H264 推流活跃时送帧最小间隔下限（ms），减轻与 vision_hub 并发 WDT */
#ifndef DEEP_DOG_FACE_AI_RTSP_MIN_INTERVAL_MS
#define DEEP_DOG_FACE_AI_RTSP_MIN_INTERVAL_MS 2000
#endif

/** RTSP 推流活跃时识别最小间隔（ms），仅运行时抬高，不写 NVS */
#ifndef DEEP_DOG_FACE_AI_RTSP_RECOG_MIN_INTERVAL_MS
#define DEEP_DOG_FACE_AI_RTSP_RECOG_MIN_INTERVAL_MS 4000
#endif

/** 1=检测前对 RGB565 每像素做高/低字节对调（仅在 INPUT_RGB888=0 时有意义） */
#ifndef DEEP_DOG_FACE_DETECT_RGB565_SWAP
#define DEEP_DOG_FACE_DETECT_RGB565_SWAP 0
#endif

/**
 * 1=先把紧密 RGB565（小端内存布局）展开为 RGB888 再送入 HumanFaceDetect
 * 为 1 时不再使用 DEEP_DOG_FACE_DETECT_RGB565_SWAP（按 LE 拆 R/G/B）
 * 默认 0：与 deep-thumble / esp-who 一致直接送 RGB565，避免每帧 ~173KB 分配失败导致静默 n=0
 */
#ifndef DEEP_DOG_FACE_DETECT_INPUT_RGB888
#define DEEP_DOG_FACE_DETECT_INPUT_RGB888 0
#endif

/**
 * MSR / MNP 阶段置信度阈值。
 * 过高（如 0.88）会压住全黑假框，但真人/照片脸也易漏检；默认对齐组件 0.5，挡镜头靠暗场门控 + min_box。
 */
#ifndef DEEP_DOG_FACE_DETECT_MSR_SCORE_THR
#define DEEP_DOG_FACE_DETECT_MSR_SCORE_THR 0.5f
#endif
#ifndef DEEP_DOG_FACE_DETECT_MNP_SCORE_THR
#define DEEP_DOG_FACE_DETECT_MNP_SCORE_THR 0.5f
#endif

/** NMS IoU 阈值 */
#ifndef DEEP_DOG_FACE_DETECT_MSR_NMS_THR
#define DEEP_DOG_FACE_DETECT_MSR_NMS_THR 0.45f
#endif
#ifndef DEEP_DOG_FACE_DETECT_MNP_NMS_THR
#define DEEP_DOG_FACE_DETECT_MNP_NMS_THR 0.45f
#endif

/** 输出前过滤宽或高小于该像素的框 */
#ifndef DEEP_DOG_FACE_DETECT_MIN_BOX_PX
#define DEEP_DOG_FACE_DETECT_MIN_BOX_PX 20
#endif

/** 1=Init 后对全黑帧跑一次检测并打 log（尺寸见 SELFTEST_W/H） */
#ifndef DEEP_DOG_FACE_DETECT_BLACK_SELFTEST_LOG
#define DEEP_DOG_FACE_DETECT_BLACK_SELFTEST_LOG 1
#endif
#ifndef DEEP_DOG_FACE_DETECT_BLACK_SELFTEST_W
#define DEEP_DOG_FACE_DETECT_BLACK_SELFTEST_W 240
#endif
#ifndef DEEP_DOG_FACE_DETECT_BLACK_SELFTEST_H
#define DEEP_DOG_FACE_DETECT_BLACK_SELFTEST_H 240
#endif

/** 1=在跑模型前对 RGB565 做稀疏采样，过暗且起伏小则跳过推理 */
#ifndef DEEP_DOG_FACE_DETECT_SKIP_UNIFORM_DARK
#define DEEP_DOG_FACE_DETECT_SKIP_UNIFORM_DARK 1
#endif
#ifndef DEEP_DOG_FACE_DETECT_UD_SAMPLE_STEP
#define DEEP_DOG_FACE_DETECT_UD_SAMPLE_STEP 12
#endif
#ifndef DEEP_DOG_FACE_DETECT_UD_MAX_MEAN_G
#define DEEP_DOG_FACE_DETECT_UD_MAX_MEAN_G 14
#endif
#ifndef DEEP_DOG_FACE_DETECT_UD_MAX_RANGE_G
#define DEEP_DOG_FACE_DETECT_UD_MAX_RANGE_G 22
#endif

/** 1=启用本地数字 ID 识别（S04）；依赖 human_face_recognition + facedb 分区 */
#ifndef DEEP_DOG_FACE_RECOG_ENABLE
#define DEEP_DOG_FACE_RECOG_ENABLE 1
#endif

/** 本地库最大 feat 条数（含 alias embedding；canonical 人数通常更少） */
#ifndef DEEP_DOG_FACE_RECOG_MAX
#define DEEP_DOG_FACE_RECOG_MAX 128
#endif

/** 自动 enroll 最低检测 score（质量门控） */
#ifndef DEEP_DOG_FACE_RECOG_MIN_ENROLL_SCORE
#define DEEP_DOG_FACE_RECOG_MIN_ENROLL_SCORE 0.55f
#endif

/** registry 条目 aliases 数组上限（单 canonical） */
#ifndef DEEP_DOG_FACE_REGISTRY_MAX_ALIASES
#define DEEP_DOG_FACE_REGISTRY_MAX_ALIASES 8
#endif

/** MQTT retain face/registry JSON 缓冲（128 槽 + Immich 字段；4096 不足会截断致前端解析失败） */
#ifndef DEEP_DOG_FACE_REGISTRY_JSON_BUF
#define DEEP_DOG_FACE_REGISTRY_JSON_BUF 32768
#endif

/** 会话去重窗口（ms）：窗口内相似则复用同一 local_id */
#ifndef DEEP_DOG_FACE_RECOG_SESSION_MS
#define DEEP_DOG_FACE_RECOG_SESSION_MS 5000
#endif

/** 1:N / 会话相似度阈值（与 DataBase query 一致） */
#ifndef DEEP_DOG_FACE_RECOG_SIM_THR
#define DEEP_DOG_FACE_RECOG_SIM_THR 0.5f
#endif

#ifndef DEEP_DOG_FACE_RECOG_DB_MOUNT
#define DEEP_DOG_FACE_RECOG_DB_MOUNT "/facedb"
#endif

#ifndef DEEP_DOG_FACE_RECOG_DB_PATH
#define DEEP_DOG_FACE_RECOG_DB_PATH "/facedb/db"
#endif

#ifndef DEEP_DOG_FACE_RECOG_NVS_NS
#define DEEP_DOG_FACE_RECOG_NVS_NS "fdog_fr"
#endif

/** 同一帧最多识别几张脸（检测可多于该数，按 score 取前 N） */
#ifndef DEEP_DOG_FACE_RECOG_MULTI_MAX
#define DEEP_DOG_FACE_RECOG_MULTI_MAX 4
#endif

/** 1=启用 Immich 真名（S05）；需 NVS 配置 api_key */
#ifndef DEEP_DOG_FACE_IMMICH_ENABLE
#define DEEP_DOG_FACE_IMMICH_ENABLE 1
#endif

#ifndef DEEP_DOG_FACE_IMMICH_NVS_NS
#define DEEP_DOG_FACE_IMMICH_NVS_NS "fdog_im"
#endif

#ifndef DEEP_DOG_FACE_IMMICH_DEFAULT_URL
#define DEEP_DOG_FACE_IMMICH_DEFAULT_URL "http://192.168.31.25:2283/api"
#endif

/** 可选：复制 face_ai_secrets.h.example → face_ai_secrets.h 注入联调 Key（勿提交） */
#if __has_include("face_ai_secrets.h")
#include "face_ai_secrets.h"
#endif
#ifndef DEEP_DOG_FACE_IMMICH_DEFAULT_API_KEY
#ifdef DEEP_DOG_FACE_IMMICH_SECRET_API_KEY
#define DEEP_DOG_FACE_IMMICH_DEFAULT_API_KEY DEEP_DOG_FACE_IMMICH_SECRET_API_KEY
#else
#define DEEP_DOG_FACE_IMMICH_DEFAULT_API_KEY ""
#endif
#endif

/** 失败后同一 local_id 的退避（秒）。联调 15；量产可改 60 */
#ifndef DEEP_DOG_FACE_IMMICH_BACKOFF_S
#define DEEP_DOG_FACE_IMMICH_BACKOFF_S 15
#endif

/** 轮询 GET /assets/{id} 次数与间隔 */
#ifndef DEEP_DOG_FACE_IMMICH_POLL_MAX
#define DEEP_DOG_FACE_IMMICH_POLL_MAX 40
#endif
#ifndef DEEP_DOG_FACE_IMMICH_POLL_MS
#define DEEP_DOG_FACE_IMMICH_POLL_MS 1500
#endif

/** 延迟 poll：对已存 asset_id 且 name_pending 的条目周期重试（秒） */
#ifndef DEEP_DOG_FACE_IMMICH_DEFERRED_POLL_S
#define DEEP_DOG_FACE_IMMICH_DEFERRED_POLL_S 300
#endif

/** 裁剪脸 JPEG 质量 */
#ifndef DEEP_DOG_FACE_IMMICH_JPEG_QUALITY
#define DEEP_DOG_FACE_IMMICH_JPEG_QUALITY 85
#endif

/** 裁剪边长下限（过小 Immich 难识别；S06 对齐短边 ≥320） */
#ifndef DEEP_DOG_FACE_IMMICH_MIN_CROP_PX
#define DEEP_DOG_FACE_IMMICH_MIN_CROP_PX 320
#endif
/** 裁剪边长上限，避免 VGA 下过大 crop/整帧 JPEG 抢内存导致 encode/WDT 失败 */
#ifndef DEEP_DOG_FACE_IMMICH_MAX_CROP_PX
#define DEEP_DOG_FACE_IMMICH_MAX_CROP_PX 400
#endif
/** 帧面积超过该值时不再整帧回退 Immich（仅 crop） */
#ifndef DEEP_DOG_FACE_IMMICH_FULLFRAME_MAX_PX
#define DEEP_DOG_FACE_IMMICH_FULLFRAME_MAX_PX (320 * 320)
#endif
/**
 * 识别流程结束后是否 DELETE 临时 asset。
 * 默认 0：保留在 Immich 便于人工核对；1=旧行为（用后即删）。
 */
#ifndef DEEP_DOG_FACE_IMMICH_DELETE_ASSET
#define DEEP_DOG_FACE_IMMICH_DELETE_ASSET 0
#endif

/**
 * FreeRTOS 任务栈（字节，internal DRAM，不可放 PSRAM）。
 * 评估方法：启用 CONFIG_FREERTOS_USE_TRACE_FACILITY 后读 uxTaskGetStackHighWaterMark
 * 或 MCP self.board.diagnostics → tasks[].stack_hwm；分配值 ≈ hwm × 1.5～2（留 HTTP/Flash 路径余量）。
 * 实测见 swrs/AUDIT_REPORT.md（face 启用后 dog_face_ai hwm≈4352，dog_immich hwm≈6580）。
 */
#ifndef DEEP_DOG_FACE_AI_TASK_STACK
#define DEEP_DOG_FACE_AI_TASK_STACK 8192
#endif
#ifndef DEEP_DOG_FACE_IMMICH_TASK_STACK
#define DEEP_DOG_FACE_IMMICH_TASK_STACK 8192
#endif
#ifndef DEEP_DOG_FACE_BOOT_TASK_STACK
#define DEEP_DOG_FACE_BOOT_TASK_STACK 16384
#endif

#endif  // _DEEP_DOG_FACE_AI_CONFIG_H_
