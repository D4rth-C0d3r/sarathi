# ADR-0003: Swappable visual themes built on semantic components

- **Status:** Accepted
- **Date:** 2026-10-20
- **Related:** ADR-0002 (vector map follows the theme)

## Context

Screens must support swappable visual themes. The first three:

- **Classic:** the readable default.
- **Fighter jet HUD:** monochrome-style, thin lines, reticle and ladder-like ornaments.
- **Racing game:** bold colours, tachometer-style arcs, chequered accents.

More will follow. The constraints:

- **Safety comes first.** Every theme must keep the one-second-glance rules: large type,
  high contrast, one primary item per screen, content inside the round safe area, knob-only
  operation. A theme can change how things look, never what is readable or how input works.
- **Flash budget.** Two 6 MB OTA slots; the firmware is about 0.65 MB today. Elecrow's
  image-heavy demo is about 6 MB, so bitmap-heavy skins don't fit.
- **LVGL 9.1.** Styles (`lv_style_t`), LVGL themes (`lv_theme_t`, default widget styles),
  compiled fonts (`lv_font_t`), and vector drawing (lines, arcs, rectangles).
- **Testability.** `sarathi_core` is pure C++ with native tests; rules that can be checked
  automatically should be.

## Decision

### Screens are composed from semantic components

Screens never set colours, fonts or sizes directly. They are built from components with a
fixed meaning, for example `ManeuverPanel`, `DistanceReadout`, `RouteMap`, `SpeedReadout`,
`CallCard`, `MediaCard`, `StatusBar`, `MenuList` and `Toast`. A component takes its data and
the active theme, and owns its layout within the round safe area.

### A theme is data

A theme has:

- **Identity:** a stable id (persisted) and a display name.
- **Palette:** semantic colour roles, not raw colours: background, surface, primary text,
  secondary text, accent, warning, danger, route line, route behind, maneuver highlight.
- **Typography:** font roles (primary, essential, supporting, stopped-only) mapped to
  compiled fonts, each at or above its minimum size below.
- **Component styles:** per-component parameters (line widths, radii, borders, arc vs bar
  readouts, label placement within limits).
- **Ornaments:** optional decorative layers drawn with vector primitives (HUD reticle and
  ticks, racing arcs). Ornaments sit behind primary content and never overlap the
  primary-content zones.

The theme **descriptor** (palette values, font roles and sizes, style parameters, ornament
choices) is plain data in `sarathi_core`, so the legibility rules below run as native tests.
`sarathi_ui` binds a descriptor to LVGL: it builds `lv_style_t` objects and picks fonts
once, at init or on theme change, and draws ornaments. LVGL's own `lv_theme_t` may be used
underneath for default widget styles.

### Selection and persistence

- Themes are compiled in and listed in a registry. **Classic** is the default.
- The rider picks a theme in Settings (so only when stopped, per the settings lock). The id
  is saved through `sarathi_settings` (NVS), with the usual debounced write.
- An unknown or missing id at boot falls back to Classic.
- Switching rebuilds styles and reloads the current screen. Input mapping, layout semantics
  and screen order are the same in every theme.

### Legibility rules every theme must pass

Checked by native tests over every registered theme where possible; the rest by review on
the device.

**Text size is set by visual angle**, the measure in-vehicle display guidance uses
(ISO 15008: character height ≥ 20 arcminutes recommended, 16 minimum). The inputs:

- Panel: 53.28 mm display area, **111 µm pixel pitch** (9.0 px/mm), 300 nit, 1000:1
  (vendor datasheet `vendor/crowpanel-2.1/Datasheet/2.1Inches Circular_P021B002-IPS(3.3V).pdf`).
- Eye to display: **75 cm**, a typical handlebar mount. To be checked on the bike in
  Phase 6; if it's further, the sizes go up.
- Font: Montserrat (LVGL built-in), cap height 0.70 of the font size.

Cap height = 75 cm × tan(angle); font px = cap height ÷ 0.111 mm ÷ 0.70.

| Text role | Visual angle | Font size |
|---|---|---|
| Primary value (maneuver distance, speed) | target ≥ 20′; floor ≥ 16′ | target ≥ 56 px; **48 px** (17′) until larger fonts exist |
| Other text needed to act while riding (call accept/reject labels, warnings) | ≥ 16′ | ≥ 46 px |
| Supporting ride-time text (street name, ETA, track title) | ≥ 12′ | ≥ 34 px |
| Screens used only when stopped (settings, menus) | ≥ 10′ | ≥ 28 px |

LVGL's built-in Montserrat stops at 48 px, so Phase 1 uses 48 px for primary values. That
passes the 16′ minimum but not the 20′ target; reaching 56 px is part of the later font
work.

The other rules:

| Rule | Value |
|---|---|
| Contrast, primary and essential text vs its background | ≥ 7:1 (WCAG 2 formula). The panel peaks at 300 nit, so sunlight washes out low-contrast pairs |
| Contrast, supporting text and route line vs background | ≥ 4.5:1 |
| Warnings and danger | never colour alone; pair with an icon or shape |
| Primary items per screen | one; ornaments are decoration only |
| Ornament motion | none while moving; nothing flashes (≤ 3 Hz) |
| Safe area | all content inside the 480 px circle with the standard margin |

### Flash budget

- No large bitmaps. Ornaments and the map are vector-drawn.
- Any single image asset ≤ 16 KB; all theme image assets together ≤ 256 KB.
- Fonts: **Phase 1 uses only LVGL's built-in Montserrat sizes** already in `lv_conf.h`.
  Custom fonts, glyph subsets and larger primary sizes come later, once the product works
  end to end; fonts are a theme's main flash cost when that happens. Unused built-in sizes
  can be switched off in `lv_conf.h` to save flash.
- Track firmware size in CI and flag growth; the image must stay well under the 6 MB slot.

## Alternatives considered

1. **LVGL `lv_theme_t` alone.** Covers default widget styles, but has no semantic colour
   roles, no ornaments and nothing to test legibility against. Kept as an implementation
   detail at most.
2. **Separate screen implementations per theme.** Screens × themes grows quickly, and every
   copy has to re-implement the safety rules. Rejected.
3. **Themes loaded at runtime from LittleFS** (descriptor files plus binary fonts). Allows
   adding themes without a firmware update, but needs parsing, runtime validation and font
   loading from the filesystem. Not now; the descriptor-as-data design keeps the door open.
4. **Bitmap skins.** Don't fit the flash budget, don't scale, and can't be checked for
   contrast. Rejected.

## Consequences

**Good**

- New themes are mostly data plus a few ornament draw functions; screens don't change.
- Legibility is enforced by tests, not taste.
- The vector map (ADR-0002) is themed for free.

**Costs and follow-ups**

- Phase 1 must design the component set before building screens.
- Theme authors work within limits; some "looks" won't be possible on a dash.
- Themes differ in palette, styles and ornaments, not typeface, until the font work. That
  work (custom typefaces, subsets, a font-generation tool) is deferred until after the
  product is functional, and will be decided then.
- Night or high-brightness variants per theme are out of scope for now.

## Implementation (Phase 1)

- **Theme data** (`sarathi_core`): `theme.h` (palette roles, font roles in px, component
  parameters, ornament id), `theme_registry` (Classic, Fighter jet HUD, Racing game),
  `theme_classic.cpp`, `theme_hud.cpp`, `theme_racing.cpp`. `legibility` implements the rules above; `test_theme_legibility` checks
  every registered theme, so a theme that breaks a rule fails CI.
- **Binding** (`sarathi_ui/theme_binding`): one shared `lv_style_t` per role. `Apply()`
  rewrites them in place and calls `lv_obj_report_style_change(nullptr)`, so a theme chosen
  in Settings restyles every screen at once, without a reboot or rebuilt screens. Drawn
  components read `theme::Current()` when they draw.
- **Semantic components** (`sarathi_ui`): `BigValue`, `ManeuverArrow` (shapes from
  `core::maneuver_glyph`), `RingGauge`, `RouteView` (geometry from `core::route_projection`,
  ADR-0002) and `ListMenu`. Screens use only these, `StaticLabel` with a text role, and
  `theme::MakeButton`.
- **Ornaments** (`core::ornament`): vector lines and rectangles as data, drawn by a
  transparent layer that `theme::MakeScreen()` puts behind everything on every screen, so
  screens don't change. They are confined to the **ornament zone**: two side strips outside
  the widest text column (|x| ≥ 195 px from the centre) and inside the ring gauge
  (r ≤ 214 px), which no screen uses. `test_theme_legibility` checks every shape against
  the zone, and a theme's ring gauge may be at most 16 px wide so it leaves the zone free.
  Ornaments are static, so the motion and flashing rules hold by construction.
  - Fighter jet HUD: a pitch-ladder scale on each side (thin spine, rungs, accent horizon).
  - Racing game: a segmented rev bar on the right, with the top segments in the danger
    colour as a redline, and a chequered strip on the left.
- **Warning text** must reach 7:1 like other text needed to act (it was 4.5:1 at first).
- The chosen theme is saved with the other settings (`sarathi_settings`, NVS).
