#include "handle/apps/handle_app_dog.h"

#include "handle/handle_config.h"
#include "handle/keymap_store.h"

#include <esp_log.h>
#include <cmath>

#if DEEP_DOG_DOG_ENABLE
#include "dog/dog_control.h"
#endif

#define TAG "handle_app_dog"

#if DEEP_DOG_DOG_ENABLE
HandleAppDog::HandleAppDog(DogControl* dog, HandleEventHub* hub) : hub_(hub), dog_(dog) {}
#else
HandleAppDog::HandleAppDog(HandleEventHub* hub) : hub_(hub) {}
#endif

void HandleAppDog::OnSnapshot(const HandleSnapshot& snap) {
#if !DEEP_DOG_DOG_ENABLE
    (void)snap;
    (void)hub_;
    return;
#else
    if (!dog_ || !hub_ || !hub_->AppsEnabled()) {
        return;
    }
    if (HandleKeymapProfile() != HANDLE_PROFILE_DOG) {
        if (move_dir_ != 0) {
            dog_->stopContinuousLocomotion();
            move_dir_ = 0;
        }
        prev_start_ = snap.buttons.start;
        prev_b_ = snap.buttons.b;
        return;
    }
    if (!snap.connected) {
        if (move_dir_ != 0) {
            dog_->stopContinuousLocomotion();
            move_dir_ = 0;
        }
        prev_start_ = snap.buttons.start;
        prev_b_ = snap.buttons.b;
        return;
    }

    if (snap.buttons.start && !prev_start_) {
        ESP_LOGI(TAG, "start -> dog.init()");
        dog_->init();
    }
    if (snap.buttons.b && !prev_b_) {
        ESP_LOGI(TAG, "b -> stop locomotion");
        dog_->stopContinuousLocomotion();
        move_dir_ = 0;
    }
    prev_start_ = snap.buttons.start;
    prev_b_ = snap.buttons.b;

    // Typical gamepad: ly negative = forward
    const float ly = snap.axes.ly;
    const float dz = DEEP_DOG_HANDLE_STICK_DEADZONE;
    int want = 0;
    if (ly < -dz) {
        want = 1;
    } else if (ly > dz) {
        want = -1;
    }

    if (want != move_dir_) {
        if (want == 0) {
            dog_->stopContinuousLocomotion();
            ESP_LOGI(TAG, "stick center -> stop");
        } else if (want > 0) {
            dog_->startContinuousForward();
            ESP_LOGI(TAG, "stick forward");
        } else {
            dog_->startContinuousBackward();
            ESP_LOGI(TAG, "stick backward");
        }
        move_dir_ = want;
    }
#endif
}
