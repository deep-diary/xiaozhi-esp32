#pragma once

#include "camera.h"
#include "config.h"
#include "touch_btn/touch_button_controller.h"

#if DEEP_DOG_DOG_ENABLE
class DogControl;
#endif

/**
 * DeepDog 板触摸按键业务逻辑（与 `TouchButtonController` 分离）。
 * - DOG 开启：键 1 长按 init / 组合键；键 2/3 行走与持续步态
 * - DOG 关闭：不链接 DogControl；仅保留可选「短按拍照解释」等与狗无关行为
 * 约定：按键编号 1/2/3 与 `TouchButtonController` 回调一致。
 */
class DeepDogTouchApp {
public:
#if DEEP_DOG_DOG_ENABLE
    explicit DeepDogTouchApp(DogControl* dog);
#else
    DeepDogTouchApp();
#endif

    /** 在 `InitializeCamera()` 之后调用，供触摸触发拍照解释。 */
    void SetCamera(Camera* camera) { camera_ = camera; }

    void OnTouchEvent(int button_id,
                      TouchButtonEvent event,
                      uint32_t value,
                      uint32_t baseline,
                      uint32_t abs_diff);

private:
#if DEEP_DOG_DOG_ENABLE
    DogControl* dog_ = nullptr;
#endif
    Camera* camera_ = nullptr;

#if DEEP_DOG_DOG_ENABLE
    int64_t combo_deadline_us_ = 0;
    bool btn1_long_fired_ = false;
    bool btn2_touching_ = false;
    bool btn2_long_fired_ = false;
    bool btn3_touching_ = false;
    bool btn3_long_fired_ = false;
    bool btn2_down_ = false;
    bool btn3_down_ = false;
    bool gesture_long2_ = false;
    bool gesture_long3_ = false;
    bool dual_stop_armed_ = false;

    static constexpr int kComboWindowMs = 3000;

    bool ComboArmed() const;
    void ArmComboAfterLongPress1();
    void DisarmCombo();
    void ExpireComboIfNeeded();

    void OnPress1();
    void OnLongPress1();
    void OnRelease1();
    void OnPress2();
    void OnPress3();
    void OnLongPress2();
    void OnLongPress3();
    void OnRelease2();
    void OnRelease3();
    void MaybeDualShortStopOnBothReleased();
    void MaybeQueuePhotoExplainAfterForward();
#else
    bool btn1_long_fired_ = false;
    void OnPress1();
    void OnLongPress1();
    void OnRelease1();
#endif

    void QueueTouchPhotoExplainIfIdle();
};
