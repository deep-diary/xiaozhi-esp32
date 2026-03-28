#pragma once

#include "camera.h"
#include "touch_btn/touch_button_controller.h"

class DogControl;

/**
 * DeepDog 板触摸按键业务逻辑（与 `TouchButtonController` 分离，便于扩展组合键）。
 * 约定：按键编号 1/2/3 与 `TouchButtonController` 回调一致。
 */
class DeepDogTouchApp {
public:
    explicit DeepDogTouchApp(DogControl* dog);

    /** 在 `InitializeCamera()` 之后调用，供触摸触发拍照解释（与 MCP take_photo 同源：先 Capture 再 Explain）。 */
    void SetCamera(Camera* camera) { camera_ = camera; }

    void OnTouchEvent(int button_id,
                      TouchButtonEvent event,
                      uint32_t value,
                      uint32_t baseline,
                      uint32_t abs_diff);

private:
    DogControl* dog_;
    Camera* camera_ = nullptr;

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
    void QueueTouchPhotoExplainIfIdle();
};
