# ADR-0019: Release and distribution

- **Status:** Accepted (2026-11-14)
- **Date:** 2026-11-14
- **Related:**
  - ADR-0004 (firmware releases and manifest signing)
  - ADR-0013 (licensing)
  - ADR-0014 (variants)
  - ADR-0015 (BLE updates)
  - ADR-0016 (core dumps)

## Context

### Releases today

- **Firmware only:** `.github/workflows/release.yml` builds **the Chronos firmware**
  (`RELEASE_ENV: crowpanel21-chronos`) when a tag is pushed.
- **Signing:** it signs the OTA manifest with a key kept in GitHub Actions secrets.
- **Size check:** it fails above 90 % of the OTA slot.
- **History:** `v0.4.0-rc.2` was the last pre-release; `v0.4.0` was never tagged.
- **The app:** it isn't released at all.

### What the owner wants

- **CI builds:** the firmware and both APK variants, reproducibly where possible.
- **Signed APKs:** the signing keystore stays with the owner, never in the repository or CI logs.
- **GitHub Releases** carry:
  - a changelog
  - a version compatibility table
  - a web flasher for first-time installs (ESP Web Tools)

## Decision

### 1. Before the first public release (checklist)

1. **Phase 3 parity:** confirmed by the rides, then the **Chronos build removed** and ADR-0005
   marked superseded. Release CI then builds `crowpanel21-phone` (pending from Phase 4).
2. **ADR-0013 applied:**
   - the Elecrow-derived code rewritten from facts (the five `sarathi_board` files and `lv_conf.h`)
   - the GFX library's licence confirmed
   - the `NOTICE` file added
3. **ADR-0014 built:** the foss flavour, runtime keys, and the CI key check.
4. **Usage policies:** each free service's policy checked again for a public app (FOSSGIS
   Valhalla, Photon, OpenFreeMap, Overpass).
5. **Tests:** the Phase 5 on-device test plan passes, including an update from the previous
   release over Wi-Fi and over BLE.
6. **Tagging:** the release tag is pushed only with the owner's OK (as for every tag).

### 2. What CI builds, on a firmware tag `vX.Y.Z` or an app tag `app-vX.Y.Z`

| Output | How |
|---|---|
| `sarathi-crowpanel21-vX.Y.Z.bin` | `pio run -e crowpanel21-phone`, the size check, as today |
| `manifest.jws` | Signed in CI with the existing ES256 key (secret), as today |
| `sarathi-crowpanel21-vX.Y.Z-factory.bin` | `esptool merge_bin` of the bootloader, partition table, `boot_app0` and the app at their offsets: one file for a first install |
| `sarathi-crowpanel21-vX.Y.Z.elf` | The firmware with symbols, for decoding core dumps (ADR-0016) |
| `sarathi-foss-vX.Y.Z-unsigned.apk` (app tags) | `./gradlew :app:assembleFossRelease` with no secrets, **unsigned** |
| full flavour | Built and tested only (`assembleFullRelease` without keys), never attached |
| `SHA256SUMS` | Of every attached file |

- **The changelog** comes from Conventional Commits (release-please, as planned in CLAUDE.md).
- **The compatibility table** is generated from the version constants, and the release notes
  include it:

  | App | Protocol (app) | Firmware | Protocol (firmware) | Works together |
  |---|---|---|---|---|

- **Separate versions** (the owner, 2026-11-14): the firmware and the app each have their own
  version and tags.
  - **Firmware:** keeps `vX.Y.Z`, which the OTA manifest and its URLs already use.
  - **App:** `app-vX.Y.Z`.
  - **Protocol:** keeps its own version (ADR-0007).
  - **The compatibility table** says which app versions work with which firmware. It's kept in
    `docs/compatibility.md` and copied into each release's notes.

### 3. Reproducible builds, where possible

- **Android:**
  - **Pinned tools:** the Gradle wrapper (with its checksum, as today), the JDK (Temurin 17), AGP
    and every dependency.
  - **No build timestamps:** none in `BuildConfig` or resources. `versionCode` and `versionName`
    come from the tag. `SOURCE_DATE_EPOCH` is set from the tagged commit's time.
  - **The check:** CI builds the unsigned foss APK twice in clean jobs and compares them
    (`diffoscope` on a mismatch).
  - **For F-Droid:** this is what lets F-Droid publish the owner-signed APK (its reproducible
    builds process).
- **Firmware:** best effort.
  - **Pinned:** the toolchain is pinned (CLAUDE.md).
  - **Why it may differ:** ESP-IDF's app descriptor may embed a compile date and time.
  - **The check:** CI builds twice and compares. If they differ only in those bytes, the release
    notes say so. Removing them needs an sdkconfig change, which is out of scope.

### 4. Signing the APK: the owner's keystore, on the owner's machine only

- **CI never sees it:** CI produces an **unsigned** APK. The owner signs it locally and uploads
  the signed APK to the release.
- **No signing key in CI:** no CI secret, no CI log, nothing in the repository. Only the
  certificate's fingerprint, which is public anyway, is written in the docs.

**Creating the keystore (once):**

```bash
mkdir -p ~/sarathi-secrets/release && chmod 700 ~/sarathi-secrets/release
keytool -genkeypair -v \
  -storetype PKCS12 \
  -keystore ~/sarathi-secrets/release/sarathi-release.p12 \
  -alias sarathi \
  -keyalg RSA -keysize 4096 -validity 10000
chmod 600 ~/sarathi-secrets/release/sarathi-release.p12
```

- **Passwords:** `keytool` prompts for the password. Never pass it on the command line, where it
  would end up in the shell history.
- **The name it asks for is public:** it's written into every APK's certificate. Use a name
  you're fine with anyone seeing (for example "sārathi"), not personal details you'd rather keep
  private.

**Recording the fingerprint (public):**

```bash
keytool -list -v -keystore ~/sarathi-secrets/release/sarathi-release.p12 -alias sarathi
```

The SHA-256 certificate fingerprint goes into `docs/` and into F-Droid's
`AllowedAPKSigningKeys`.

**Backing up (before the first release):**

- **Two offline copies** of the `.p12`:
  - one on an encrypted USB drive kept at home
  - one somewhere else: an encrypted drive elsewhere, or a password manager's file attachment
- **The password** in a password manager, never next to the file.
- **Test a restore:** sign a test APK with a backup copy and compare its certificate.
- **Why it matters:** **losing the key means existing installs can never be updated.** Users
  would have to uninstall, and their bonds and settings would be lost.

**Signing a release:**

```bash
sha256sum -c SHA256SUMS --ignore-missing      # the APK CI built
apksigner sign --ks ~/sarathi-secrets/release/sarathi-release.p12 --ks-key-alias sarathi \
  --out sarathi-foss-vX.Y.Z.apk sarathi-foss-vX.Y.Z-unsigned.apk
apksigner verify --print-certs sarathi-foss-vX.Y.Z.apk
```

`apksigner` prompts for the password. The signed APK is attached to the release, with its
SHA-256.

### 5. Web flasher (first-time firmware install)

- **The page:** an **ESP Web Tools** page on the repository's GitHub Pages site.
  - **Contents:** an install button, the manifest ESP Web Tools needs (pointing at the release's
    factory image), and short instructions:
    - use the USB IN port
    - hold BOOT while plugging in if it doesn't connect
    - only Chrome or Edge on a computer (Web Serial)
- **The files are copied to GitHub Pages with each release.** GitHub's release downloads redirect
  to a host that may not send the CORS headers a web page needs (**to verify**). The owner's own
  network also failed on that host once (OTA, 2026-11-09).
- **First install erases the flash** (ESP Web Tools' erase prompt), which is expected for a new
  device. Later updates go over the air (Wi-Fi or BLE), never through the flasher.
- **New web dependency (flagged):** ESP Web Tools loads from a pinned version on a CDN.

### 6. Where the APK is published

- **GitHub Releases:** the signed foss APK.
- **F-Droid:** after ADR-0014's question about native libraries is answered. F-Droid builds from
  the tag and publishes the owner-signed APK if its build matches.
- **Google Play:** not now. It would need a developer account, a fee and Play's policies; that's
  a separate decision.

## Consequences

- **`release.yml`:** grows into a firmware job and an Android job, with the reproducibility
  checks.
- **GitHub Pages:** a new workflow for the flasher page.
- **Docs:**
  - `docs/building.md` gains "Releasing", with the signing steps above.
  - `SECURITY.md` gains how releases are signed and verified.
- **The keystore** is created by the owner before the first release. Claude never creates or
  handles it.
