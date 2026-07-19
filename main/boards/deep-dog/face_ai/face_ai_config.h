#ifndef _DEEP_DOG_FACE_AI_CONFIG_H_
#define _DEEP_DOG_FACE_AI_CONFIG_H_

/**
 * 网页人脸检测（human_face_detect / ESP-DL）相关配置（从 board `config.h` 拆分）。
 */

/** 1=编译进固件；0=桩实现，零推理开销 */
#ifndef DEEP_DOG_FACE_AI_ENABLE
#define DEEP_DOG_FACE_AI_ENABLE 1
#endif

/** 向人脸任务送帧的最小间隔（ms），用于降载 */
#ifndef DEEP_DOG_FACE_AI_MIN_INTERVAL_MS
#define DEEP_DOG_FACE_AI_MIN_INTERVAL_MS 250
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

/** 1=Init 后对 240×240 全黑帧跑一次检测并打 log */
#ifndef DEEP_DOG_FACE_DETECT_BLACK_SELFTEST_LOG
#define DEEP_DOG_FACE_DETECT_BLACK_SELFTEST_LOG 1
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

/** 本地库最大人数 */
#ifndef DEEP_DOG_FACE_RECOG_MAX
#define DEEP_DOG_FACE_RECOG_MAX 16
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

/** 失败后同一 local_id 的退避（秒） */
#ifndef DEEP_DOG_FACE_IMMICH_BACKOFF_S
#define DEEP_DOG_FACE_IMMICH_BACKOFF_S 60
#endif

/** 轮询 GET /assets/{id} 次数与间隔 */
#ifndef DEEP_DOG_FACE_IMMICH_POLL_MAX
#define DEEP_DOG_FACE_IMMICH_POLL_MAX 40
#endif
#ifndef DEEP_DOG_FACE_IMMICH_POLL_MS
#define DEEP_DOG_FACE_IMMICH_POLL_MS 1500
#endif

/** 裁剪脸 JPEG 质量 */
#ifndef DEEP_DOG_FACE_IMMICH_JPEG_QUALITY
#define DEEP_DOG_FACE_IMMICH_JPEG_QUALITY 85
#endif

/** 裁剪边长下限（过小 Immich 难识别） */
#ifndef DEEP_DOG_FACE_IMMICH_MIN_CROP_PX
#define DEEP_DOG_FACE_IMMICH_MIN_CROP_PX 160
#endif

#endif  // _DEEP_DOG_FACE_AI_CONFIG_H_
