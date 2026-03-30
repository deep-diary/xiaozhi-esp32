#ifndef _DEEP_DOG_TOUCH_CONFIG_H_
#define _DEEP_DOG_TOUCH_CONFIG_H_

/**
 * 触摸按键行为相关配置（从 board `config.h` 拆分）。
 */

/**
 * 触摸触发「先 Capture 再 Explain」（与 MCP `self.camera.take_photo` 一致）：
 * - 键 1：短按释放（未触发长按初始化）时排队；
 * - 键 2：成功 goForward 一小步后排队。
 * 需已配置图像解释 URL/Token。置 0 可关闭。
 */
#ifndef DEEP_DOG_TOUCH2_SHORT_PHOTO_EXPLAIN
#define DEEP_DOG_TOUCH2_SHORT_PHOTO_EXPLAIN 0
#endif

#endif  // _DEEP_DOG_TOUCH_CONFIG_H_
