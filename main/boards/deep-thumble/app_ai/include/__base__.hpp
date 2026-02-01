#pragma once

#include <list>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// esp_camera.h 来自 esp32-camera 组件（参考例 human_face_recognition_no_wakenet 依赖该组件）。
// deep-thumble 使用 esp_video（Esp32Camera），未引入 esp32-camera，此处无 esp_camera 时仅前向声明。
#if __has_include("esp_camera.h")
#include "esp_camera.h"
#else
struct camera_fb_t;
inline void esp_camera_fb_return(camera_fb_t *fb) { (void)fb; }
#endif

typedef enum
{
    COMMAND_TIMEOUT = -2,
    COMMAND_NOT_DETECTED = -1,

    MENU_STOP_WORKING = 0,
    MENU_DISPLAY_ONLY = 1,
    MENU_FACE_RECOGNITION = 2,
    MENU_MOTION_DETECTION = 3,

    ACTION_ENROLL = 4,
    ACTION_DELETE = 5,
    ACTION_RECOGNIZE = 6
} command_word_t;

class Observer
{
public:
    virtual void update() = 0;
};

class Subject
{
private:
    std::list<Observer *> observers;

public:
    void attach(Observer *observer)
    {
        this->observers.push_back(observer);
    }

    void detach(Observer *observer)
    {
        this->observers.remove(observer);
    }

    void detach_all()
    {
        this->observers.clear();
    }

    void notify()
    {
        for (auto observer : this->observers)
            observer->update();
    }
};

class Frame
{
public:
    QueueHandle_t queue_i;
    QueueHandle_t queue_o;
    void (*callback)(camera_fb_t *);

    Frame(QueueHandle_t queue_i = nullptr,
          QueueHandle_t queue_o = nullptr,
          void (*callback)(camera_fb_t *) = nullptr) : queue_i(queue_i),
                                                       queue_o(queue_o),
                                                       callback(callback) {}

    void set_io(QueueHandle_t queue_i, QueueHandle_t queue_o)
    {
        this->queue_i = queue_i;
        this->queue_o = queue_o;
    }
};
