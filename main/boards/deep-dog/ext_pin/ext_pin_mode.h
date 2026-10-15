#ifndef _DEEP_DOG_EXT_PIN_MODE_H_
#define _DEEP_DOG_EXT_PIN_MODE_H_

#include <stddef.h>

#include <esp_err.h>

#include "config.h"

/**
 * 自由引出脚（GPIO38/48）运行时模式（CFG-02）。
 *
 * 编译期：38/48 相关软件栈**全部编入**（见 config.h / board_features.h），
 * 运行期：由本模块决定**当前生效**模式，并持久化到 NVS。
 *
 * 生效方式 v1 = 重启生效：
 *   配置中心下发 io.ext_pins.mode → SetPendingMode() 写入 NVS
 *   → 重启 → 板级 ResolveBootTarget() 取值并初始化
 *   → 成功 CommitApplied()，失败 RollbackMode() 回滚
 */
namespace deep_dog_ext_pin {

/** 无待生效模式（PendingMode 返回值） */
constexpr deep_dog_ext_pin_mode_t kNoPending = -1;

/** 编译期默认模式（config.h 的 DEEP_DOG_EXT_PIN_MODE） */
deep_dog_ext_pin_mode_t DefaultMode();

/** 本固件实际编入的模式白名单（用于 supported_modes 上报与下发校验） */
const deep_dog_ext_pin_mode_t* SupportedModes(size_t* count);
bool IsSupported(deep_dog_ext_pin_mode_t mode);

const char* ModeToStr(deep_dog_ext_pin_mode_t mode);
/** 字符串 → 模式；无法识别时返回 false 且不改 out */
bool StrToMode(const char* str, deep_dog_ext_pin_mode_t* out);

/** 启动目标：NVS pending（合法）> NVS 上次生效 > 编译默认 */
deep_dog_ext_pin_mode_t ResolveBootTarget();

/** 启动回滚目标：上次成功生效模式；不可用或与 failed 相同则返回 NONE */
deep_dog_ext_pin_mode_t RollbackMode(deep_dog_ext_pin_mode_t failed);

/** 目标模式应用成功：记为生效模式并清除 pending */
void CommitApplied(deep_dog_ext_pin_mode_t mode);

/** 记录一次应用失败（上报用），失败原因见 LastApplyError */
void MarkApplyFailure(const char* code);
const char* LastApplyError();

/** 本进程当前生效模式 */
deep_dog_ext_pin_mode_t ActiveMode();
bool IsActive(deep_dog_ext_pin_mode_t mode);

/** 待重启生效模式；无则 kNoPending（含校验） */
deep_dog_ext_pin_mode_t PendingMode();

/** 下发/设置待生效模式：校验白名单并写 NVS。非法返回 ESP_ERR_INVALID_ARG */
esp_err_t SetPendingMode(deep_dog_ext_pin_mode_t mode);
void ClearPending();

/** 生效策略（CFG-02 v1 固定 "restart"） */
const char* ApplyPolicy();

}  // namespace deep_dog_ext_pin

#endif  // _DEEP_DOG_EXT_PIN_MODE_H_
