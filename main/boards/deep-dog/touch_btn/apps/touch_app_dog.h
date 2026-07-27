#pragma once

#include "touch_btn/touch_app.h"
#include "touch_btn/touch_config.h"
#include "camera.h"
#include "config.h"

#if DEEP_DOG_DOG_ENABLE
class DogControl;
#endif

/**
 * 四足运控触摸映射（与驱动解耦）。
 * - DOG 开：键1 长按 init + 组合窗；键2/3 press 小步；长按持续走；组合窗 short 大步
 * - DOG 关：仅保留键1 short_press 可选拍照解释
 */
class TouchAppDog : public ITouchApp {
public:
#if DEEP_DOG_DOG_ENABLE
    explicit TouchAppDog(DogControl* dog);
#else
    TouchAppDog();
#endif

    const char* Name() const override { return "dog"; }

    void SetCamera(Camera* camera) { camera_ = camera; }

    void OnEvent(const TouchEvent& ev) override;

private:
    Camera* camera_ = nullptr;

#if DEEP_DOG_DOG_ENABLE
    DogControl* dog_ = nullptr;

    int64_t combo_deadline_us_ = 0;
    bool btn2_touching_ = false;
    bool btn3_touching_ = false;
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
    void OnShortPress1();
    void OnPress2();
    void OnPress3();
    void OnLongPress2();
    void OnLongPress3();
    void OnShortPress2();
    void OnShortPress3();
    void OnRelease2();
    void OnRelease3();
    void MaybeDualShortStopOnBothReleased();
    void MaybeQueuePhotoExplainAfterForward();
#else
    void OnShortPress1();
#endif

    void QueueTouchPhotoExplainIfIdle();
};
