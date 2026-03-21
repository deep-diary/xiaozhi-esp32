#include "deep_dog_touch_app.h"
#include "dog/dog_control.h"

#include <esp_log.h>
#include <esp_timer.h>

#define TAG "dog_touch"

DeepDogTouchApp::DeepDogTouchApp(DogControl* dog) : dog_(dog) {}

bool DeepDogTouchApp::ComboArmed() const {
    if (combo_deadline_us_ == 0) {
        return false;
    }
    return (int64_t)esp_timer_get_time() < combo_deadline_us_;
}

void DeepDogTouchApp::ArmComboAfterLongPress1() {
    combo_deadline_us_ = (int64_t)esp_timer_get_time() + (int64_t)kComboWindowMs * 1000;
    ESP_LOGI(TAG, "组合键窗口：长按1 后 %d ms 内短按2=站立 / 短按3=趴下", kComboWindowMs);
}

void DeepDogTouchApp::DisarmCombo() {
    combo_deadline_us_ = 0;
}

void DeepDogTouchApp::ExpireComboIfNeeded() {
    if (combo_deadline_us_ != 0 && (int64_t)esp_timer_get_time() >= combo_deadline_us_) {
        DisarmCombo();
    }
}

void DeepDogTouchApp::OnLongPress1() {
    if (!dog_) {
        return;
    }
    if (dog_->init()) {
        ESP_LOGI(TAG, "Touch: dog init ok");
    } else {
        ESP_LOGE(TAG, "Touch: dog init failed");
    }
    ArmComboAfterLongPress1();
}

void DeepDogTouchApp::OnPress2() {
    if (!dog_) {
        return;
    }
    if (!ComboArmed()) {
        if (dog_->goForward()) {
            ESP_LOGI(TAG, "Touch: goForward 1 step ok");
        } else {
            ESP_LOGE(TAG, "Touch: goForward failed");
        }
        return;
    }
    btn2_touching_ = true;
    btn2_long_fired_ = false;
}

void DeepDogTouchApp::OnPress3() {
    if (!dog_) {
        return;
    }
    if (!ComboArmed()) {
        if (dog_->goBack()) {
            ESP_LOGI(TAG, "Touch: goBack 1 step ok");
        } else {
            ESP_LOGE(TAG, "Touch: goBack failed");
        }
        return;
    }
    btn3_touching_ = true;
    btn3_long_fired_ = false;
}

void DeepDogTouchApp::OnLongPress2() {
    btn2_long_fired_ = true;
    if (!dog_) {
        return;
    }
    if (dog_->goForwardSteps(5)) {
        ESP_LOGI(TAG, "Touch: goForwardSteps(5) ok");
    } else {
        ESP_LOGE(TAG, "Touch: goForwardSteps(5) failed");
    }
}

void DeepDogTouchApp::OnLongPress3() {
    btn3_long_fired_ = true;
    if (!dog_) {
        return;
    }
    if (dog_->goBackSteps(5)) {
        ESP_LOGI(TAG, "Touch: goBackSteps(5) ok");
    } else {
        ESP_LOGE(TAG, "Touch: goBackSteps(5) failed");
    }
}

void DeepDogTouchApp::OnRelease2() {
    if (!btn2_touching_) {
        return;
    }
    btn2_touching_ = false;
    const bool was_short = !btn2_long_fired_;
    btn2_long_fired_ = false;

    if (!dog_) {
        return;
    }
    if (ComboArmed() && was_short) {
        if (dog_->stand()) {
            ESP_LOGI(TAG, "Touch: 组合键 长按1→短按2 → stand ok");
        } else {
            ESP_LOGE(TAG, "Touch: stand failed");
        }
        DisarmCombo();
    }
}

void DeepDogTouchApp::OnRelease3() {
    if (!btn3_touching_) {
        return;
    }
    btn3_touching_ = false;
    const bool was_short = !btn3_long_fired_;
    btn3_long_fired_ = false;

    if (!dog_) {
        return;
    }
    if (ComboArmed() && was_short) {
        if (dog_->lieDown()) {
            ESP_LOGI(TAG, "Touch: 组合键 长按1→短按3 → lieDown ok");
        } else {
            ESP_LOGE(TAG, "Touch: lieDown failed");
        }
        DisarmCombo();
    }
}

void DeepDogTouchApp::OnTouchEvent(int button_id,
                                   TouchButtonEvent event,
                                   uint32_t value,
                                   uint32_t baseline,
                                   uint32_t abs_diff) {
    (void)value;
    (void)baseline;
    (void)abs_diff;

    ExpireComboIfNeeded();

    switch (event) {
        case TouchButtonEvent::kPress:
            ESP_LOGI(TAG, "Touch button %d pressed", button_id);
            if (button_id == 2) {
                OnPress2();
            } else if (button_id == 3) {
                OnPress3();
            }
            break;

        case TouchButtonEvent::kRelease:
            ESP_LOGI(TAG, "Touch button %d released", button_id);
            if (button_id == 2) {
                OnRelease2();
            } else if (button_id == 3) {
                OnRelease3();
            }
            break;

        case TouchButtonEvent::kLongPress:
            ESP_LOGI(TAG, "Touch button %d long-pressed", button_id);
            if (button_id == 1) {
                OnLongPress1();
            } else if (button_id == 2) {
                OnLongPress2();
            } else if (button_id == 3) {
                OnLongPress3();
            }
            break;
    }
}
