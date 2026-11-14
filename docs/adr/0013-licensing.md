# ADR-0013: Licensing

- **Status:** Accepted (2026-11-14): Apache-2.0 stays. The Elecrow question (§3) waits for the
  owner.
- **Date:** 2026-11-14
- **Related:**
  - ADR-0006 (maps backends)
  - ADR-0009 (map data and provenance)
  - ADR-0014 (build variants)
  - ADR-0019 (release)
  - the licensing audit of 2026-10-24, which was deferred until after Phase 4

## Context

sārathi is Apache-2.0 today (`LICENSE`). The owner wants a public release (Phase 5) and asked
for the licence decision that was deferred from Phase 4:

- GPL-3.0 or the current licence?
- What every dependency allows?
- Who must agree to a change?
- What OpenStreetMap's licence asks of the device and the app?

### Who holds copyright

`git log` has **one human author**, the owner, on all 160 commits (as of 2026-11-14).

- **The Claude co-author lines:** 159 commits also carry `Co-Authored-By: Claude`, which marks
  AI assistance. Claude is not a person and holds no copyright, so no consent is needed from it.
- **How much of that text is protectable** by copyright varies by jurisdiction. That doesn't
  block a licence change; it only means some of it may not be protectable at all.
- **No other person has contributed.** Relicensing needs nobody else's consent today. Once others
  contribute, it would.

### Dependency inventory

Versions as pinned in `firmware/platformio.ini` and `android/gradle/libs.versions.toml`. Licences
were read from the packages themselves (library `LICENSE` files, Maven POMs) unless marked "to
confirm".

**Firmware (shipped in the image)**

| Component | Version | Licence | In a GPL-3.0 project? |
|---|---|---|---|
| ESP-IDF (via pioarduino Arduino core 3.3.8) | 5.5.4 | Apache-2.0 | Yes (Apache-2.0 is compatible with GPL-3.0) |
| Espressif binary-only libraries (Wi-Fi, BLE controller, PHY, coexistence) | with ESP-IDF | Apache-2.0, **no source** | Needs a **GPL section 7 additional permission** to link them: GPL-3.0 wants the Corresponding Source, which nobody can provide for them |
| FreeRTOS (in ESP-IDF) | | MIT | Yes |
| mbedTLS (in ESP-IDF) | | Apache-2.0 | Yes |
| Arduino-ESP32 core | 3.3.8 | LGPL-2.1+ (core), Apache-2.0 (most libraries) (to confirm) | Yes (LGPL-2.1+ and Apache-2.0 both fit GPL-3.0) |
| LVGL | 9.1.0 | MIT | Yes |
| LVGL's built-in fonts | 9.1.0 | Montserrat: SIL OFL 1.1; symbols from Font Awesome Free (to confirm) | Yes (fonts are separate works under OFL) |
| NimBLE-Arduino | 2.5.1 | Apache-2.0 | Yes |
| GFX Library for Arduino | 1.6.7 | **No licence file in the PlatformIO package**; derived from Adafruit-GFX (BSD) | **To confirm** with the upstream repository before release |
| Adafruit CST8XX Library | 1.1.1 | MIT (in the source headers) | Yes |
| Adafruit BusIO | 1.16.2 | MIT | Yes |
| PCF8574 library (Renzo Mischianti) | 2.3.7 | MIT | Yes |
| ChronosESP32, ESP32Time | 1.9.1, 2.0.6 | MIT | Removed before release (Phase 4 pending) |
| **Elecrow example code** (`vendor/crowpanel-2.1`) | | **No licence: all rights reserved** | **No**, not under any licence (below) |

**Android app**

| Component | Version | Licence | In a GPL-3.0 project? |
|---|---|---|---|
| Kotlin stdlib, coroutines, kotlinx-serialization | 2.4.21, 1.11.0, 1.12.0 | Apache-2.0 | Yes |
| AndroidX (core, lifecycle, activity, Compose BOM) | see `libs.versions.toml` | Apache-2.0 | Yes |
| OkHttp | 5.5.1 | Apache-2.0 | Yes |
| Ferrostar core | 0.59.0 | BSD-3-Clause | Yes |
| valhalla-mobile, valhalla-models-config | 0.6.4, 0.6.0 | MIT | Yes |
| desugar_jdk_libs_nio | 2.1.5 | GPL-2.0 with the Classpath Exception | Yes (the exception allows linking) |
| **Google Navigation SDK** | 8.0.0 | Google Maps Platform Terms of Service (proprietary) | **Only with an additional permission**, and only in builds that include it (ADR-0014) |
| **Google Places SDK** | 6.0.2 | Google Maps Platform Terms of Service (proprietary) | Same |
| **Mappls REST SDK and services plugin** | 6.0.3, 1.0.1 | MapmyIndia licence (proprietary) | Same |
| Transitive dependencies (Ferrostar's JNA and uniffi, Google Play services, …) | | various | **To list:** a full transitive licence report is generated when ADR-0014 is built (flagged: that needs a Gradle licence-report plugin, a new build dependency) |
| Build and test only (JUnit EPL-1.0, ktlint, detekt, AGP) | | various | Not shipped; no effect |

**Map data and services**

| Source | Licence or terms | Duty |
|---|---|---|
| OpenStreetMap data (every map chunk, the offline region, Valhalla's offline tiles, Overpass answers) | ODbL 1.0 | Attribution; share-alike for derivative databases that are distributed (below) |
| OpenFreeMap tiles (OpenMapTiles schema) | Data ODbL; schema CC-BY 4.0 | "OpenFreeMap © OpenMapTiles Data from OpenStreetMap" |
| Protomaps basemap (the offline region) | Data ODbL | "Protomaps © OpenStreetMap" |
| Photon (search), FOSSGIS Valhalla, GraphHopper | Each service's terms | Service attribution, and their usage policies (ADR-0014) |
| Google, Mappls | Their terms | As in ADR-0006 |

### The Elecrow problem

Elecrow's repository has no licence, so all rights are reserved. Code adapted from it is
`include/lv_conf.h` and `sarathi_board`'s display, expander, touch and knob drivers, plus
`lvgl_port`.

- **It can't be distributed** under Apache-2.0, GPL-3.0 or anything else.
- **Facts can stay:** pin numbers, timings, the init command sequence as data, and the
  power-up order.
- **The code must be rewritten** from those facts and the datasheets, or licensed from Elecrow in
  writing.
- **This blocks any public release,** whatever licence is chosen.

## Options

### A. Stay on Apache-2.0

- **Pros:**
  - Nothing to change; the simplest option.
  - The proprietary SDKs and Espressif's binary libraries need no extra permissions.
  - Fine for F-Droid.
- **Cons:**
  - Anyone may make a closed, commercial fork, including of the firmware.
  - No dual-licensing business is possible: the owner can't sell commercial licences to an
    already permissive codebase.

### B. GPL-3.0-only, with two section 7 additional permissions

The two permissions:

1. **Espressif:** linking Espressif's binary-only system libraries.
2. **Proprietary SDKs:** linking the Google Navigation and Places SDKs and the Mappls SDK. The
   wording allows the "full" variant (ADR-0014) to be built and distributed.

- **Pros:**
  - Forks must stay open.
  - The owner can still offer commercial licences, as the only copyright holder.
  - Fine for F-Droid (the "foss" variant needs neither permission's second part).
- **Cons:**
  - Contributions from others then need a **contributor licence agreement** (for example the
    Harmony CLA), or dual licensing is lost.
  - The permissions must be worded carefully. They are standard practice (the "linking exception"
    pattern), but get them reviewed.
  - Some companies avoid GPL code.

### C. GPL-3.0-or-later

- As B, but the FSF's future GPL versions also apply.
- Not recommended: it hands future licence terms to a third party.

## Decision

1. **Licence:** **Option A: Apache-2.0 stays** (the owner, 2026-11-14).
   - **No exceptions needed:** the proprietary SDKs and Espressif's binary libraries need no
     additional permissions under Apache-2.0.
   - **No CLA needed:** contributions come in under Apache-2.0 (inbound = outbound).
   - **Recorded:** the GPL options were considered, and the comparison is kept above.
2. **Elecrow code** (§3 below): **open, waiting for the owner.** The options are:
   - **(a) Rewrite** the adapted files from facts and datasheets.
   - **(b) Ask Elecrow** for a licence.
   - **(c) Accept the risk.**

   Recommended: (b), then (a) if there's no answer. Either way, it's settled before the first
   public release (ADR-0019 checklist).
3. **GFX Library for Arduino:** confirm its licence upstream. If it has none, replace it with
   ESP-IDF's `esp_lcd` RGB panel driver, which the renderer work (ADR-0009) already considered.
4. **ODbL duties:**
   - **Device:**
     - "(c) OpenStreetMap" whenever the map is shown: 10 s after the map appears (step G4), and
       with the backend's attribution while stopped.
     - A Settings row "Map data: OpenStreetMap (ODbL)".
     - Kept in every theme, inside the circle, at the stopped-only size.
   - **App:**
     - "© OpenStreetMap contributors" on every map it draws. The missing-roads map already does
       this.
     - The data source's credit: OpenFreeMap, OpenMapTiles or Protomaps.
     - An About screen with every licence, including ODbL with a link and the services' terms.
   - **Derivative databases:**
     - The chunks are made on each user's phone for their own dash. They aren't distributed, so
       share-alike doesn't apply.
     - The **offline region and Valhalla's offline tiles** are derivative databases. If a public
       APK bundled them, they would be distributed. So public builds don't bundle them (ADR-0014):
       they're downloaded separately under ODbL, and the scripts that make them are public in
       `tools/`.
5. **NOTICE file:** a `NOTICE` file at the root lists third-party components and their licences,
   as Apache-2.0 asks of redistributed works. The app's About screen shows the same list.

## Consequences

- **Elecrow decision first:** the Elecrow decision and the GFX check come before the first public
  release.
- **No licence change:** no relicensing work. `LICENSE` stays Apache-2.0; a `NOTICE` file is
  added.
