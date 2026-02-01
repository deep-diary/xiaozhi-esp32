#include "who_ai_utils.hpp"

#include "esp_log.h"
#include "esp_heap_caps.h"

#if __has_include("esp_camera.h")
#include "esp_camera.h"
#endif

#include "dl_image.hpp"
#include "dl_image_define.hpp"

static const char *TAG = "ai_utils";

static constexpr uint8_t LINE_WIDTH = 2;
static constexpr uint8_t POINT_RADIUS = 4;

// RGB565 颜色：绿框 0b1110000000000111，红点 0b0000000011111000，蓝点 0b0001111100000000
static void draw_detection_result_rgb565(uint16_t *image_ptr, int image_height, int image_width, std::list<dl::detect::result_t> &results)
{
    dl::image::img_t img = {
        .data = image_ptr,
        .width = static_cast<uint16_t>(image_width),
        .height = static_cast<uint16_t>(image_height),
        .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565,
    };
    uint16_t green565 = 0x07E0;
    dl::image::pix_t pix_green = {.data = &green565, .type = dl::image::DL_IMAGE_PIX_TYPE_RGB565};
    uint16_t red565 = 0xF800;
    dl::image::pix_t pix_red = {.data = &red565, .type = dl::image::DL_IMAGE_PIX_TYPE_RGB565};
    uint16_t blue565 = 0x001F;
    dl::image::pix_t pix_blue = {.data = &blue565, .type = dl::image::DL_IMAGE_PIX_TYPE_RGB565};

    for (auto prediction = results.begin(); prediction != results.end(); prediction++)
    {
        int x1 = DL_MAX(prediction->box[0], 0);
        int y1 = DL_MAX(prediction->box[1], 0);
        int x2 = DL_MAX(prediction->box[2], 0);
        int y2 = DL_MAX(prediction->box[3], 0);
        dl::image::draw_hollow_rectangle<uint16_t>(img, x1, y1, x2, y2, LINE_WIDTH, pix_green);

        if (prediction->keypoint.size() == 10)
        {
            dl::image::draw_point<uint16_t>(img, DL_MAX(prediction->keypoint[0], 0), DL_MAX(prediction->keypoint[1], 0), POINT_RADIUS, pix_red);   // left eye
            dl::image::draw_point<uint16_t>(img, DL_MAX(prediction->keypoint[2], 0), DL_MAX(prediction->keypoint[3], 0), POINT_RADIUS, pix_red);   // mouth left
            dl::image::draw_point<uint16_t>(img, DL_MAX(prediction->keypoint[4], 0), DL_MAX(prediction->keypoint[5], 0), POINT_RADIUS, pix_green);  // nose
            dl::image::draw_point<uint16_t>(img, DL_MAX(prediction->keypoint[6], 0), DL_MAX(prediction->keypoint[7], 0), POINT_RADIUS, pix_blue);  // right eye
            dl::image::draw_point<uint16_t>(img, DL_MAX(prediction->keypoint[8], 0), DL_MAX(prediction->keypoint[9], 0), POINT_RADIUS, pix_blue);  // mouth right
        }
    }
}

void draw_detection_result(uint16_t *image_ptr, int image_height, int image_width, std::list<dl::detect::result_t> &results)
{
    draw_detection_result_rgb565(image_ptr, image_height, image_width, results);
}

void draw_detection_result(uint8_t *image_ptr, int image_height, int image_width, std::list<dl::detect::result_t> &results)
{
    dl::image::img_t img = {
        .data = image_ptr,
        .width = static_cast<uint16_t>(image_width),
        .height = static_cast<uint16_t>(image_height),
        .pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888,
    };
    uint8_t green888[3] = {0x00, 0xFF, 0x00};
    uint8_t red888[3] = {0xFF, 0x00, 0x00};
    uint8_t blue888[3] = {0x00, 0x00, 0xFF};
    dl::image::pix_t pix_green = {.data = green888, .type = dl::image::DL_IMAGE_PIX_TYPE_RGB888};
    dl::image::pix_t pix_red = {.data = red888, .type = dl::image::DL_IMAGE_PIX_TYPE_RGB888};
    dl::image::pix_t pix_blue = {.data = blue888, .type = dl::image::DL_IMAGE_PIX_TYPE_RGB888};

    for (auto prediction = results.begin(); prediction != results.end(); prediction++)
    {
        int x1 = DL_MAX(prediction->box[0], 0);
        int y1 = DL_MAX(prediction->box[1], 0);
        int x2 = DL_MAX(prediction->box[2], 0);
        int y2 = DL_MAX(prediction->box[3], 0);
        dl::image::draw_hollow_rectangle<uint8_t>(img, x1, y1, x2, y2, LINE_WIDTH, pix_green);

        if (prediction->keypoint.size() == 10)
        {
            dl::image::draw_point<uint8_t>(img, DL_MAX(prediction->keypoint[0], 0), DL_MAX(prediction->keypoint[1], 0), POINT_RADIUS, pix_red);
            dl::image::draw_point<uint8_t>(img, DL_MAX(prediction->keypoint[2], 0), DL_MAX(prediction->keypoint[3], 0), POINT_RADIUS, pix_red);
            dl::image::draw_point<uint8_t>(img, DL_MAX(prediction->keypoint[4], 0), DL_MAX(prediction->keypoint[5], 0), POINT_RADIUS, pix_green);
            dl::image::draw_point<uint8_t>(img, DL_MAX(prediction->keypoint[6], 0), DL_MAX(prediction->keypoint[7], 0), POINT_RADIUS, pix_blue);
            dl::image::draw_point<uint8_t>(img, DL_MAX(prediction->keypoint[8], 0), DL_MAX(prediction->keypoint[9], 0), POINT_RADIUS, pix_blue);
        }
    }
}

void print_detection_result(std::list<dl::detect::result_t> &results)
{
    int i = 0;
    for (std::list<dl::detect::result_t>::iterator prediction = results.begin(); prediction != results.end(); prediction++, i++)
    {
        ESP_LOGI("detection_result", "[%2d]: (%3d, %3d, %3d, %3d)", i, prediction->box[0], prediction->box[1], prediction->box[2], prediction->box[3]);

        if (prediction->keypoint.size() == 10)
        {
            ESP_LOGI("detection_result", "      left eye: (%3d, %3d), right eye: (%3d, %3d), nose: (%3d, %3d), mouth left: (%3d, %3d), mouth right: (%3d, %3d)",
                     prediction->keypoint[0], prediction->keypoint[1],  // left eye
                     prediction->keypoint[6], prediction->keypoint[7],  // right eye
                     prediction->keypoint[4], prediction->keypoint[5],  // nose
                     prediction->keypoint[2], prediction->keypoint[3],  // mouth left corner
                     prediction->keypoint[8], prediction->keypoint[9]); // mouth right corner
        }
    }
}

void *app_camera_decode(camera_fb_t *fb)
{
#if __has_include("esp_camera.h")
    if (fb->format == PIXFORMAT_RGB565)
    {
        return (void *)fb->buf;
    }
    else
    {
        const size_t rgb888_size = (size_t)fb->height * fb->width * 3 * sizeof(uint8_t);
        uint8_t *image_ptr = (uint8_t *)heap_caps_malloc(rgb888_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (image_ptr)
        {
            if (fmt2rgb888(fb->buf, fb->len, fb->format, image_ptr))
            {
                return (void *)image_ptr;
            }
            else
            {
                ESP_LOGE(TAG, "fmt2rgb888 failed");
                heap_caps_free(image_ptr);
            }
        }
        else
        {
            ESP_LOGE(TAG, "alloc memory for image rgb888 failed (PSRAM)");
        }
    }
#else
    (void)fb;
#endif
    return NULL;
}