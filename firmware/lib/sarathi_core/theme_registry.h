// The compiled-in themes. Classic is the default and the fallback (ADR-0003).

#pragma once

#include <cstddef>

#include "theme.h"

namespace sarathi::core {

extern const ThemeDescriptor kClassicTheme;
extern const ThemeDescriptor kFighterHudTheme;
extern const ThemeDescriptor kRacingTheme;

[[nodiscard]] size_t ThemeCount();
[[nodiscard]] const ThemeDescriptor& ThemeAt(size_t index);  // 0 <= index < ThemeCount()

// The theme with this id, or Classic if it isn't registered.
[[nodiscard]] const ThemeDescriptor& ThemeFor(ThemeId id);

[[nodiscard]] bool IsRegistered(ThemeId id);

}  // namespace sarathi::core
