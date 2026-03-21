#pragma once

#include "touch_btn/touch_button_controller.h"

class DogControl;

/**
 * DeepDog 板触摸按键业务逻辑（与 `TouchButtonController` 分离，便于扩展组合键）。
 *
 * 约定：按键编号 1/2/3 与 `TouchButtonController` 回调一致。
 */
class DeepDogTouchApp {
public:
    explicit DeepDogTouchApp(DogControl* dog);

    void OnTouchEvent(int button_id,
                      TouchButtonEvent event,
                      uint32_t value,
                      uint32_t baseline,
                      uint32_t abs_diff);

private:
    DogControl* dog_;

    /** 长按 1 完成后的一段时间内允许「短按 2 / 短按 3」组合（站立 / 趴下） */
    int64_t combo_deadline_us_ = 0;

    /** 区分短按与长按：按下后若未发生 long press 即释放，视为短按 */
    bool btn2_touching_ = false;
    bool btn2_long_fired_ = false;
    bool btn3_touching_ = false;
    bool btn3_long_fired_ = false;

    static constexpr int kComboWindowMs = 3000;

    bool ComboArmed() const;
    void ArmComboAfterLongPress1();
    void DisarmCombo();
    void ExpireComboIfNeeded();

    void OnLongPress1();
    void OnPress2();
    void OnPress3();
    void OnLongPress2();
    void OnLongPress3();
    void OnRelease2();
    void OnRelease3();
};
