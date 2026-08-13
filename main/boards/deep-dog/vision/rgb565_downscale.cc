#include "vision/rgb565_downscale.h"

bool Rgb565DownscaleHalf(const uint8_t* src, uint16_t src_w, uint16_t src_h, uint8_t* dst,
                         uint16_t dst_w, uint16_t dst_h) {
    if (!src || !dst || src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0) {
        return false;
    }
    if (src_w != dst_w * 2 || src_h != dst_h * 2) {
        return false;
    }
    const size_t src_row_bytes = static_cast<size_t>(src_w) * 2u;
    const size_t dst_row_bytes = static_cast<size_t>(dst_w) * 2u;
    for (uint16_t dy = 0; dy < dst_h; ++dy) {
        const uint8_t* src_row = src + static_cast<size_t>(dy * 2) * src_row_bytes;
        uint8_t* dst_row = dst + static_cast<size_t>(dy) * dst_row_bytes;
        for (uint16_t dx = 0; dx < dst_w; ++dx) {
            const size_t sx = static_cast<size_t>(dx) * 2u * 2u;
            dst_row[dx * 2] = src_row[sx];
            dst_row[dx * 2 + 1] = src_row[sx + 1];
        }
    }
    return true;
}

size_t Rgb565DownscaleHalfBytes(uint16_t dst_w, uint16_t dst_h) {
    if (dst_w == 0 || dst_h == 0) {
        return 0;
    }
    return static_cast<size_t>(dst_w) * static_cast<size_t>(dst_h) * 2u;
}
