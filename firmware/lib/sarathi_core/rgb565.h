// RGB565 helpers. Pure logic.

#pragma once

#include <cstdint>

namespace sarathi::core {

// Swaps the 5-bit red and blue fields; green is unchanged.
constexpr uint16_t SwapRedBlue(uint16_t rgb565) {
  return static_cast<uint16_t>((rgb565 & 0x07E0U) | ((rgb565 & 0x001FU) << 11U) |
                               ((rgb565 & 0xF800U) >> 11U));
}

}  // namespace sarathi::core
