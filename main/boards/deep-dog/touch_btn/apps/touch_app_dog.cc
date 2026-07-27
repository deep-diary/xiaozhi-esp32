#include "touch_btn/apps/touch_app_dog.h"
#include "touch_btn/touch_config.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <new>
#include <string>

#if DEEP_DOG_DOG_ENABLE
#include "dog/dog_control.h"
#include "motor/motor_config.h"
#endif

#define TAG "touch_app_dog"

namespace {

std::atomic<bool> g_touch_explain_busy{false};

void TouchExplainTask(void* arg) {
    struct Job {
        Camera* cam;
    };
    auto* job = static_cast<Job*>(arg);
    try {
        constexpr const char* kDefaultQuestion = "请解释下这张图片。";
        if (!job->cam->Capture()) {
            ESP_LOGE(TAG, "触摸拍照问答：采帧失败（与 MCP take_photo 一致需先 Capture）");
        } else {
            const std::string r = job->cam->Explain(kDefaultQuestion);
            ESP_LOGI(TAG, "触摸拍照问答结果: %s", r.c_str());
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "触摸拍照问答失败: %s", e.what());
    } catch (...) {
        ESP_LOGE(TAG, "触摸拍照问答失败: 未知异常");
    }
    delete job;
    g_touch_explain_busy.store(false);
    vTaskDelete(nullptr);
}

}  // namespace

#if DEEP_DOG_DOG_ENABLE
TouchAppDog::TouchAppDog(DogControl* dog) : dog_(dog) {}
#else
TouchAppDog::TouchAppDog() = default;
#endif

void TouchAppDog::QueueTouchPhotoExplainIfIdle() {
#if !DEEP_DOG_TOUCH2_SHORT_PHOTO_EXPLAIN
    return;
#endif
    if (!camera_) {
        return;
    }
    bool expected = false;
    if (!g_touch_explain_busy.compare_exchange_strong(expected, true)) {
        ESP_LOGW(TAG, "上一路拍照问答尚未结束，跳过本次");
        return;
    }
    struct Job {
        Camera* cam;
    };
    Job* job = new (std::nothrow) Job{camera_};
    if (!job) {
        g_touch_explain_busy.store(false);
        ESP_LOGE(TAG, "拍照问答任务内存不足");
        return;
    }
    constexpr uint32_t kStackWords = 12288;
    if (xTaskCreate(TouchExplainTask, "touch_explain", kStackWords, job, 5, nullptr) != pdPASS) {
        delete job;
        g_touch_explain_busy.store(false);
        ESP_LOGE(TAG, "touch_explain 任务创建失败");
    }
}

void TouchAppDog::OnShortPress1() {
    QueueTouchPhotoExplainIfIdle();
}

#if DEEP_DOG_DOG_ENABLE

void TouchAppDog::MaybeQueuePhotoExplainAfterForward() {
    QueueTouchPhotoExplainIfIdle();
}

bool TouchAppDog::ComboArmed() const {
    if (combo_deadline_us_ == 0) {
        return false;
    }
    return (int64_t)esp_timer_get_time() < combo_deadline_us_;
}

void TouchAppDog::ArmComboAfterLongPress1() {
    combo_deadline_us_ = (int64_t)esp_timer_get_time() + (int64_t)kComboWindowMs * 1000;
    ESP_LOGI(TAG,
             "组合键窗口：长按1 后 %d ms 内 短按2/3=前进/后退一大步；长按2=站立 长按3=趴下",
             kComboWindowMs);
}

void TouchAppDog::DisarmCombo() {
    combo_deadline_us_ = 0;
    btn2_touching_ = false;
    btn3_touching_ = false;
}

void TouchAppDog::ExpireComboIfNeeded() {
    if (combo_deadline_us_ != 0 && (int64_t)esp_timer_get_time() >= combo_deadline_us_) {
        DisarmCombo();
    }
}

void TouchAppDog::OnPress1() {}

void TouchAppDog::OnLongPress1() {
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

void TouchAppDog::OnPress2() {
    if (!dog_) {
        return;
    }
    btn2_down_ = true;
    gesture_long2_ = false;

    if (ComboArmed()) {
        btn2_touching_ = true;
        return;
    }

    if (dog_->isContinuousLocomotionActive()) {
        if (btn3_down_) {
            dual_stop_armed_ = true;
        }
        return;
    }

    if (btn3_down_) {
        dual_stop_armed_ = true;
    }

    if (dog_->goForward()) {
        ESP_LOGI(TAG, "Touch: goForward 一小步 ok（press2）");
    } else {
        ESP_LOGE(TAG, "Touch: goForward failed");
    }
    MaybeQueuePhotoExplainAfterForward();
}

void TouchAppDog::OnPress3() {
    if (!dog_) {
        return;
    }
    btn3_down_ = true;
    gesture_long3_ = false;

    if (ComboArmed()) {
        btn3_touching_ = true;
        return;
    }

    if (dog_->isContinuousLocomotionActive()) {
        if (btn2_down_) {
            dual_stop_armed_ = true;
        }
        return;
    }

    if (btn2_down_) {
        dual_stop_armed_ = true;
    }

    if (dog_->goBack()) {
        ESP_LOGI(TAG, "Touch: goBack 一小步 ok（press3）");
    } else {
        ESP_LOGE(TAG, "Touch: goBack failed");
    }
}

void TouchAppDog::OnLongPress2() {
    gesture_long2_ = true;
    dual_stop_armed_ = false;
    if (!dog_) {
        return;
    }
    if (ComboArmed()) {
        if (dog_->stand()) {
            ESP_LOGI(TAG, "Touch: 组合键 长按1→长按2 → stand ok");
        } else {
            ESP_LOGE(TAG, "Touch: stand failed（组合窗口内长按2）");
        }
        DisarmCombo();
        return;
    }
    if (dog_->startContinuousForward()) {
        ESP_LOGI(TAG, "Touch: 持续前进（长按2）");
    } else {
        ESP_LOGE(TAG, "Touch: 持续前进启动失败");
    }
}

void TouchAppDog::OnLongPress3() {
    gesture_long3_ = true;
    dual_stop_armed_ = false;
    if (!dog_) {
        return;
    }
    if (ComboArmed()) {
        if (dog_->lieDown()) {
            ESP_LOGI(TAG, "Touch: 组合键 长按1→长按3 → lieDown ok");
        } else {
            ESP_LOGE(TAG, "Touch: lieDown failed（组合窗口内长按3）");
        }
        DisarmCombo();
        return;
    }
    if (dog_->startContinuousBackward()) {
        ESP_LOGI(TAG, "Touch: 持续后退（长按3）");
    } else {
        ESP_LOGE(TAG, "Touch: 持续后退启动失败");
    }
}

void TouchAppDog::MaybeDualShortStopOnBothReleased() {
    if (!dog_) {
        return;
    }
    if (btn2_down_ || btn3_down_) {
        return;
    }
    if (dual_stop_armed_ && !gesture_long2_ && !gesture_long3_) {
        if (dog_->isContinuousLocomotionActive()) {
            dog_->stopContinuousLocomotion();
            ESP_LOGI(TAG, "Touch: 短按 2+3 → 停止持续行走（保持当前姿态）");
        }
    }
    dual_stop_armed_ = false;
    gesture_long2_ = false;
    gesture_long3_ = false;
}

void TouchAppDog::OnShortPress2() {
    if (!dog_) {
        return;
    }
    if (ComboArmed() && btn2_touching_) {
        btn2_touching_ = false;
        if (dog_->goForwardBigStep(DEEP_DOG_MIT_VDES_RAD_S, 40)) {
            ESP_LOGI(TAG, "Touch: 组合键 长按1→短按2 → goForwardBigStep ok");
        } else {
            ESP_LOGE(TAG, "Touch: goForwardBigStep failed");
        }
        DisarmCombo();
    }
}

void TouchAppDog::OnShortPress3() {
    if (!dog_) {
        return;
    }
    if (ComboArmed() && btn3_touching_) {
        btn3_touching_ = false;
        if (dog_->goBackBigStep(DEEP_DOG_MIT_VDES_RAD_S, 40)) {
            ESP_LOGI(TAG, "Touch: 组合键 长按1→短按3 → goBackBigStep ok");
        } else {
            ESP_LOGE(TAG, "Touch: goBackBigStep failed");
        }
        DisarmCombo();
    }
}

void TouchAppDog::OnRelease2() {
    if (!dog_) {
        return;
    }
    btn2_down_ = false;
    MaybeDualShortStopOnBothReleased();
}

void TouchAppDog::OnRelease3() {
    if (!dog_) {
        return;
    }
    btn3_down_ = false;
    MaybeDualShortStopOnBothReleased();
}

void TouchAppDog::OnEvent(const TouchEvent& ev) {
    ExpireComboIfNeeded();

    switch (ev.event) {
        case TouchButtonEvent::kPress:
            if (ev.button_id == 1) {
                OnPress1();
            } else if (ev.button_id == 2) {
                OnPress2();
            } else if (ev.button_id == 3) {
                OnPress3();
            }
            break;

        case TouchButtonEvent::kRelease:
            if (ev.button_id == 2) {
                OnRelease2();
            } else if (ev.button_id == 3) {
                OnRelease3();
            }
            break;

        case TouchButtonEvent::kLongPress:
            if (ev.button_id == 1) {
                OnLongPress1();
            } else if (ev.button_id == 2) {
                OnLongPress2();
            } else if (ev.button_id == 3) {
                OnLongPress3();
            }
            break;

        case TouchButtonEvent::kShortPress:
            if (ev.button_id == 1) {
                OnShortPress1();
            } else if (ev.button_id == 2) {
                OnShortPress2();
            } else if (ev.button_id == 3) {
                OnShortPress3();
            }
            break;

        case TouchButtonEvent::kDoubleClick:
            break;
    }
}

#else  // !DEEP_DOG_DOG_ENABLE

void TouchAppDog::OnEvent(const TouchEvent& ev) {
    if (ev.event == TouchButtonEvent::kShortPress && ev.button_id == 1) {
        OnShortPress1();
    } else if (ev.event == TouchButtonEvent::kLongPress && ev.button_id == 1) {
        ESP_LOGI(TAG, "Touch: dog disabled — long-press1 ignored (no DogControl)");
    }
}

#endif  // DEEP_DOG_DOG_ENABLE
