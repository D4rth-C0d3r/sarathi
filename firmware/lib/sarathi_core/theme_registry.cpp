#include "theme_registry.h"

#include <array>

namespace sarathi::core {
namespace {

// Settings lists themes in this order. Classic first: it is the default.
const std::array<const ThemeDescriptor*, 2> kThemes = {&kClassicTheme, &kFighterHudTheme};

}  // namespace

size_t ThemeCount() { return kThemes.size(); }

const ThemeDescriptor& ThemeAt(size_t index) {
  return index < kThemes.size() ? *kThemes.at(index) : kClassicTheme;
}

const ThemeDescriptor& ThemeFor(ThemeId id) {
  for (const ThemeDescriptor* theme : kThemes) {
    if (theme->id == id) {
      return *theme;
    }
  }
  return kClassicTheme;
}

bool IsRegistered(ThemeId id) {
  for (const ThemeDescriptor* theme : kThemes) {
    if (theme->id == id) {
      return true;
    }
  }
  return false;
}

}  // namespace sarathi::core
