#pragma once

#include <cstddef>
#include <cstdint>

/**
 * RGB565 2× 降采样（src 宽高须为 dst 的 2 倍）。
 * 对 2×2 块取左上角像素（nearest-neighbor），适用于 640×480 → 320×240。
 */
bool Rgb565DownscaleHalf(const uint8_t* src, uint16_t src_w, uint16_t src_h, uint8_t* dst,
                         uint16_t dst_w, uint16_t dst_h);

/** 返回 dst 字节数（w*h*2），失败返回 0。 */
size_t Rgb565DownscaleHalfBytes(uint16_t dst_w, uint16_t dst_h);
