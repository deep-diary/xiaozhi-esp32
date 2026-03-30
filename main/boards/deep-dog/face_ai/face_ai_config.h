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
 */
#ifndef DEEP_DOG_FACE_DETECT_INPUT_RGB888
#define DEEP_DOG_FACE_DETECT_INPUT_RGB888 1
#endif

/** MSR / MNP 阶段置信度阈值 */
#ifndef DEEP_DOG_FACE_DETECT_MSR_SCORE_THR
#define DEEP_DOG_FACE_DETECT_MSR_SCORE_THR 0.88f
#endif
#ifndef DEEP_DOG_FACE_DETECT_MNP_SCORE_THR
#define DEEP_DOG_FACE_DETECT_MNP_SCORE_THR 0.88f
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

#endif  // _DEEP_DOG_FACE_AI_CONFIG_H_
