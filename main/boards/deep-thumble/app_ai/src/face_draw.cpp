// 在 RGB565 缓冲上绘制人脸框和简单文字
// 参考：who_ai_utils.hpp 的 draw_detection_result()；此处为最小实现（框 + ID/姓名）

#include "face_draw.hpp"

#include <cstring>
#include <cstdio>
#include <algorithm>

namespace {

constexpr uint16_t kColorFaceBox = 0x001F;
constexpr uint16_t kColorWhite = 0xFFFF;
constexpr uint16_t kColorBlack = 0x0000;

inline void set_pixel(uint16_t* buf, int stride, int x, int y, int w, int h, uint16_t color) {
    if (x >= 0 && x < w && y >= 0 && y < h) {
        buf[y * stride / 2 + x] = color;
    }
}

void draw_rect(uint16_t* buf, int stride_pixels, int x, int y, int rw, int rh, int w, int h, uint16_t color, int thickness = 2) {
    for (int t = 0; t < thickness; t++) {
        int left = x + t;
        int top = y + t;
        int right = x + rw - 1 - t;
        int bottom = y + rh - 1 - t;
        for (int i = left; i <= right; i++) {
            set_pixel(buf, stride_pixels * 2, i, top, w, h, color);
            set_pixel(buf, stride_pixels * 2, i, bottom, w, h, color);
        }
        for (int j = top; j <= bottom; j++) {
            set_pixel(buf, stride_pixels * 2, left, j, w, h, color);
            set_pixel(buf, stride_pixels * 2, right, j, w, h, color);
        }
    }
}

constexpr int CHAR_W = 5;
constexpr int CHAR_H = 7;

void draw_char_5x7(uint16_t* buf, int stride_pixels, int x0, int y0, char c, int w, int h, uint16_t fg, uint16_t bg) {
    bool pat[5][7] = {};
    auto draw_pixel = [&](int px, int py) {
        set_pixel(buf, stride_pixels * 2, x0 + px, y0 + py, w, h, fg);
    };
    if (c >= '0' && c <= '9') {
        int n = c - '0';
        switch (n) {
            case 0: for (int i = 0; i < 5; i++) { pat[i][0]=true; pat[i][6]=true; } for (int j = 1; j < 6; j++) { pat[0][j]=true; pat[4][j]=true; } break;
            case 1: for (int j = 0; j < 7; j++) pat[2][j]=true; break;
            case 2: for (int i = 0; i < 5; i++) pat[i][0]=pat[i][3]=pat[i][6]=true; for (int j = 1; j < 3; j++) pat[4][j]=true; for (int j = 4; j < 6; j++) pat[0][j]=true; break;
            case 3: for (int i = 0; i < 5; i++) pat[i][0]=pat[i][3]=pat[i][6]=true; for (int j = 1; j < 3; j++) pat[4][j]=true; for (int j = 4; j < 6; j++) pat[4][j]=true; break;
            case 4: for (int j = 0; j < 7; j++) pat[4][j]=true; pat[0][0]=pat[1][0]=pat[0][1]=pat[1][1]=pat[0][2]=pat[1][2]=pat[2][2]=pat[3][2]=true; break;
            case 5: for (int i = 0; i < 5; i++) pat[i][0]=pat[i][3]=pat[i][6]=true; for (int j = 1; j < 3; j++) pat[0][j]=true; for (int j = 4; j < 6; j++) pat[4][j]=true; break;
            case 6: for (int i = 0; i < 5; i++) pat[i][0]=pat[i][3]=pat[i][6]=true; for (int j = 1; j < 6; j++) pat[0][j]=true; for (int j = 4; j < 6; j++) pat[4][j]=true; break;
            case 7: for (int i = 0; i < 5; i++) pat[i][0]=true; for (int j = 1; j < 7; j++) pat[4][j]=true; break;
            case 8: for (int i = 0; i < 5; i++) pat[i][0]=pat[i][3]=pat[i][6]=true; for (int j = 1; j < 3; j++) pat[0][j]=pat[4][j]=true; for (int j = 4; j < 6; j++) pat[0][j]=pat[4][j]=true; break;
            case 9: for (int i = 0; i < 5; i++) pat[i][0]=pat[i][3]=pat[i][6]=true; for (int j = 1; j < 3; j++) pat[0][j]=pat[4][j]=true; for (int j = 4; j < 6; j++) pat[4][j]=true; break;
            default: break;
        }
    } else if (c == '?') {
        pat[2][0]=true; for (int j = 1; j < 5; j++) pat[0][j]=pat[4][j]=true; pat[2][4]=true; pat[2][6]=true;
    }
    for (int py = 0; py < 7; py++)
        for (int px = 0; px < 5; px++)
            if (pat[px][py]) draw_pixel(px, py);
}

}  // namespace

namespace deep_thumble {

void DrawFaceBoxesOnRgb565(uint8_t* rgb565_buf, uint16_t buf_w, uint16_t buf_h,
                           const std::vector<FaceBox>& boxes) {
    if (rgb565_buf == nullptr || buf_w == 0 || buf_h == 0) return;
    uint16_t* buf = reinterpret_cast<uint16_t*>(rgb565_buf);
    int stride = buf_w;

    for (const auto& b : boxes) {
        int x = std::max(0, b.x);
        int y = std::max(0, b.y);
        int rw = std::min(b.width, static_cast<int>(buf_w) - x);
        int rh = std::min(b.height, static_cast<int>(buf_h) - y);
        if (rw <= 0 || rh <= 0) continue;

        draw_rect(buf, stride, x, y, rw, rh, buf_w, buf_h, kColorFaceBox, 5);

        char id_buf[12];
        snprintf(id_buf, sizeof(id_buf), "%d", b.id);
        int label_x = x;
        int label_y = std::max(0, y - CHAR_H - 2);
        for (int i = 0; id_buf[i]; i++) {
            draw_char_5x7(buf, stride, label_x + 2 + i * (CHAR_W + 1), label_y + 1, id_buf[i], buf_w, buf_h, kColorWhite, kColorBlack);
        }
        label_x += (int)strlen(id_buf) * (CHAR_W + 1) + 4;
        for (size_t i = 0; i < b.name.size() && i < 6; i++) {
            char ch = b.name[i];
            if ((ch >= '0' && ch <= '9') || ch == '?') {
                draw_char_5x7(buf, stride, label_x + (int)i * (CHAR_W + 1), label_y + 1, ch, buf_w, buf_h, kColorWhite, kColorBlack);
            }
        }
    }
}

}  // namespace deep_thumble
