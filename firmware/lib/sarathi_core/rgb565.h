// RGB565 helpers. Pure logic.

#pragma once

#include <cstdint>

namespace sarathi::core {

// Swaps the 5-bit red and blue fields; green is unchanged.
constexpr uint16_t SwapRedBlue(uint16_t rgb565) {
  return static_cast<uint16_t>((rgb565 & 0x07E0U) | ((rgb565 & 0x001FU) << 11U) |
                               ((rgb565 & 0xF800U) >> 11U));
}

// 0xRRGGBB with red and blue exchanged: the colour to hand LVGL so that the pixels it renders
// are already in the order of a panel that swaps the two (ADR-0009: no swap pass at flush).
constexpr uint32_t SwapRedBlueRgb(uint32_t rgb) {
  return (rgb & 0x00FF00U) | ((rgb >> 16U) & 0xFFU) | ((rgb & 0xFFU) << 16U);
}

// 0xRRGGBB to RGB565 the way LVGL converts it (top bits of each channel).
constexpr uint16_t ToRgb565(uint32_t rgb) {
  return static_cast<uint16_t>(((rgb >> 8U) & 0xF800U) | ((rgb >> 5U) & 0x07E0U) |
                               ((rgb >> 3U) & 0x001FU));
}

}  // namespace sarathi::core
