# CLAUDE.md — android/

Rules for working in the Android companion app. The root `CLAUDE.md` still applies (plan
first, small steps, flag new dependencies, keep docs in sync). Setup and commands for humans
are in `docs/building.md`.

## Status

Phase 4 step D: setup (permissions, companion pairing) and the ride link, a foreground service
that keeps the dash connected with the screen off (ADR-0010). `:core:protocol` implements
protocol v1; `:core:ble` the session and the Bluetooth transport. Navigation, calls and music
come in later steps. Don't add features or permissions unasked.

## Commands (run from `android/`)

```bash
./gradlew lint testDebugUnitTest assembleDebug   # what CI runs, plus the line below
./gradlew ktlintCheck detekt
./gradlew ktlintFormat                           # fix ktlint findings; don't hand-format around it
./gradlew installDebug                           # needs a phone with USB debugging
```

Before finishing any Android change, run both check lines and report the result. CI
(`.github/workflows/android.yml`) runs the same five tasks.

## Modules and dependencies

| Module | Kind | Owns |
|---|---|---|
| `:app` | Android application | Activity, Compose UI, navigation, ViewModels, wiring |
| `:core:protocol` | **pure Kotlin/JVM** | Message types, JSON encode/decode, protocol version |
| `:core:ble` | Android library | Scan, bond, GATT connection, MTU, reconnects |
| `:feature:*` (later) | Android library | `navigation`, `media`, `calls`, `ota` |

- Dependencies point inward: `app → feature → core:ble → core:protocol`. Features never
  depend on each other or on `:app`. `core:protocol` depends on nothing in this repo.
- **`core:protocol` stays pure Kotlin.** No `android.*` imports, no Android plugin. Its tests
  run on the JVM against the golden vectors in `docs/protocol/vectors/`, the same files the
  firmware tests use. It has a `testDebugUnitTest` alias so the standard command runs them.
- `core:protocol` has **no library dependencies**: it has its own strict JSON parser and
  canonical writer (`Json.kt`), matching the firmware's `JsonReader` byte for byte. Use them
  for protocol messages; other modules may still propose kotlinx-serialization for other JSON.
- Messages are data classes that check the spec's limits in `init`, so anything that encodes
  is valid. Decode errors are `ProtocolException.reason`, the vectors' error names.
- New modules: add them to `settings.gradle.kts` and copy the shape of an existing module.
  If shared build config grows past a few lines, propose `build-logic/` convention plugins
  instead of growing the root `allprojects {}` block.

## Toolchain and versions

- Every version lives in `gradle/libs.versions.toml`, including SDK levels and the Java
  target. Never hard-code a version in a build script.
- AGP 9 compiles Kotlin itself: **don't apply `org.jetbrains.kotlin.android`**. Compose needs
  only `org.jetbrains.kotlin.plugin.compose`.
- compileSdk/targetSdk 37, minSdk 26, Java/Kotlin target 17. Namespace and applicationId
  `io.github.d4rthc0d3r.sarathi`; never change the applicationId.
- Upgrades (Gradle, AGP, Kotlin, Compose BOM, ktlint, detekt) go in a dedicated PR. Gradle
  wrapper changes must keep `distributionSha256Sum`; CI validates the wrapper jar.
- Configuration cache is on. Don't read files or environment variables at configuration
  time in ways that break it; use `providers.*`.
- Adding any library is a dependency change: flag it and ask first (root rule). That
  includes DI (Hilt/Koin), serialization, Robolectric and test frameworks.

## Code style

- ktlint (official style) and detekt with `config/detekt/detekt.yml` on top of the defaults.
  `@Composable` functions are PascalCase; that's already allowed in both tools.
- Don't silence findings with `@Suppress` or config changes unless there's a real reason;
  put the reason in a comment next to it.
- Compose: stateless composables take state and lambdas; `Modifier` is the first optional
  parameter; add a `@Preview` for each screen. Strings go in `res/values/strings.xml`.
- Architecture: MVVM with unidirectional data flow. ViewModels expose `StateFlow<UiState>`
  and take events as function calls. Coroutines for async; no callbacks leaking out of
  `core:ble`.

## Protocol

- **Spec first.** Any change to messages updates `docs/protocol/protocol.md`, `schema/` and
  `vectors/` before Kotlin or firmware code. No message types exist until that spec does.
- The protocol version is separate from the app version and is sent in `hello`.
- Unknown fields and message types must be tolerated, not crash the app.

## BLE, permissions and background work (Phase 4 guidance)

- The phone is the brain (ADR-0001): it gathers nav, call and media data and pushes it to
  the device; the device sends back knob actions. Audio never goes to the device.
- One GATT service: write characteristic (phone → device), notify characteristic
  (device → phone), request MTU ≥ 247.
- Control and OTA writes need a bonded link; the user confirms bonding with a knob press.
  Prefer `CompanionDeviceManager` for pairing.
- Every new permission needs a stated reason in the PR and an ask before adding. Expect:
  `BLUETOOTH_SCAN` (with `neverForLocation`) and `BLUETOOTH_CONNECT` on API 31+, legacy
  `BLUETOOTH`/`BLUETOOTH_ADMIN`/location with `maxSdkVersion="30"`, a `connectedDevice`
  foreground service for the ride, notification-listener access for nav and media, and
  `ANSWER_PHONE_CALLS` for accept/reject.
- Handle disconnects as normal: reconnect with backoff, never spin.
- **How it's done (ADR-0010):** `DashLink` reconnects with `autoConnect` (the chip waits, no
  polling); `RideLinkService` adds the `location` type only when Android allows it at that
  start. No `BLUETOOTH_SCAN`: the companion chooser finds the dash.
- **Test on the phone over wireless debugging** (`docs/building.md`); force Doze with
  `adb shell dumpsys deviceidle force-idle`. The emulator has no Bluetooth.

## Rider safety and security

- Nothing in the app should need attention while riding. Setup happens before the ride;
  during it the phone stays in a pocket and the device is glance-only.
- Never commit keystores, `local.properties` or signing secrets (all gitignored). Release
  signing will come from CI secrets.
- Backup and device transfer are disabled (`backup_rules.xml`, `data_extraction_rules.xml`):
  bonds and settings are per phone.

## Gotchas

- `local.properties` (`sdk.dir=...`) or `ANDROID_HOME` must point at the SDK. On the
  maintainer's laptop it's `~/Android/Sdk` (command-line tools, no Android Studio).
- Recent SDK command-line tools print that `sdkmanager` is deprecated in favour of the new
  `android` CLI; `sdkmanager` still works.
- `ktlintFormat` wraps long call chains (for example `libs.versions.x.get().toInt()`) over
  several lines. That's the official style; leave it.
- Gradle prints "deprecated features ... incompatible with Gradle 10". These come from the
  detekt 1.23 and ktlint plugins, not our scripts.
- A first build downloads Gradle, AGP and dependencies; a transient network error on the
  wrapper download can be retried.
