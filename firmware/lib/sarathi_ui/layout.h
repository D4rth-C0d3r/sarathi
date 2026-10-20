// Screen geometry shared by every theme (ADR-0003: themes change appearance, not layout).

#pragma once

#include <cstdint>

namespace sarathi::ui::layout {

constexpr int32_t kScreenSize = 480;
constexpr int32_t kCenter = kScreenSize / 2;
constexpr int32_t kContentWidth = 380;  // widest centred text line inside the circle

}  // namespace sarathi::ui::layout
