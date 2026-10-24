# ADR-0004: Wi-Fi OTA from GitHub Releases, signed images and rollback

- **Status:** Accepted
- **Date:** 2026-10-22
- **Related:** ADR-0001 (BLE only; Wi-Fi is for OTA only), ADR-0003 (settings, themes)

## Context

Phase 2 adds over-the-air updates over Wi-Fi. The constraints:

- **Flash:** two 6 MB app slots (`app0`, `app1` in `firmware/partitions.csv`) and an
  `otadata` partition. The image is 930,480 B today (14.8 % of a slot).
- **No keyboard.** The device has a knob and a small touch screen. Typing a Wi-Fi password
  on it is impractical.
- **No backend.** The project is open source with no server of its own. Releases already
  happen on GitHub.
- **Safety.** A bad update must never leave a bike with a dead dash. Updating must not
  invite interaction while riding, and flash writes can make the RGB panel flicker.
- **Security (CLAUDE.md).** Firmware images are signed, and the signing key exists only in
  GitHub Actions secrets. OTA uses rollback. The app marks itself valid only after the UI
  and BLE start successfully.
- **Pinned toolchain.** Arduino core 3.3.8 / ESP-IDF 5.5.4 ships **precompiled** ESP-IDF
  libraries and a prebuilt bootloader. Changing `sdkconfig` means rebuilding them, so the
  design must work with the shipped configuration where possible.

### What the pinned core provides (verified, not assumed)

All of the following was checked in `~/.platformio/packages/framework-arduinoespressif32-libs/esp32s3/`
(versions.txt: esp-idf v5.5.4, arduino idf-v5.5.4) and in
`framework-arduinoespressif32` 3.3.8:

| Item | Finding | Where |
|---|---|---|
| App rollback | `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` (so `CONFIG_APP_ROLLBACK_ENABLE`) | `sdkconfig` line 422; also `qio_opi/include/sdkconfig.h` (our memory type) |
| Bootloader | Built with rollback: the prebuilt `bin/bootloader_qio_80m.elf` (our flash mode) contains `ESP_OTA_IMG_PENDING_VERIFY`, `ESP_OTA_IMG_ABORTED` and `set_actual_ota_seq` | `bin/` |
| Anti-rollback (eFuse) | Off (`CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK` not set) | `sdkconfig` |
| Secure boot / signed apps | Off (`CONFIG_SECURE_BOOT`, `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` not set) | `sdkconfig` |
| Arduino auto-validation | `initArduino()` marks a `PENDING_VERIFY` app **valid at boot** unless the sketch overrides the weak `bool verifyRollbackLater()` to return `true` | `cores/esp32/esp32-hal-misc.c` |
| Root CA bundle | `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y`, full Mozilla set (`_DEFAULT_FULL`); data linked as `_binary_x509_crt_bundle_start`, 68,983 B | `sdkconfig`; `lib/libmbedtls.a` (`x509_crt_bundle.S.obj`) |
| Certificate dates | `CONFIG_MBEDTLS_HAVE_TIME_DATE` **not set**: mbedTLS doesn't check certificate validity periods | `sdkconfig` |
| ECDSA P-256 | `CONFIG_MBEDTLS_ECDSA_C=y`, `CONFIG_MBEDTLS_ECP_DP_SECP256R1_ENABLED=y` | `sdkconfig` |
| Plain-HTTP OTA | Disabled in `esp_https_ota` (`CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP` not set) | `sdkconfig` |
| Signed updates in Arduino | `Update` has optional signing (`-DUPDATE_SIGN`, `Updater_Signing.h`): a 512-byte signature **appended** to the image, checked in `Update.end()` | `libraries/Update/src` |

**Rollback is already enabled**, in the configuration and in the bootloader we flash. It
costs nothing extra. The only firmware work is to override `verifyRollbackLater()` so
Arduino doesn't mark every new image valid before our own checks run.

### Standards and reference designs

The design follows what production IoT update systems do, and the published guidance for
them:

- **ETSI EN 303 645** (consumer IoT baseline), provision 5.3: updates are delivered
  securely, their authenticity and integrity are verified on the device before
  installation, and updating is simple for the user.
- **NIST SP 800-193** (platform firmware resiliency): *protection* (only authenticated
  updates are installed), *detection* (corrupt or unauthorised images are found before they
  run), *recovery* (fall back to a known-good image).
- **IETF SUIT architecture, RFC 9019**: a signed manifest that names the device class,
  carries a sequence number for anti-rollback, the image digest and size, and where to get
  it. The device trusts the manifest's signature, and the image through the digest in it.
- **Reference designs:**
  - Azure Device Update: a JWS-signed import manifest carrying SHA-256 file hashes.
  - AWS IoT OTA: an ECDSA P-256 / SHA-256 code-signing signature delivered beside the
    image, not inside it.
  - Espressif: dual OTA slots with bootloader rollback.

## Decision

### 1. Source: GitHub Releases plus a signed manifest (JWS)

Each firmware release (tag `vX.Y.Z`) publishes two assets: the unmodified app image
`sarathi-crowpanel21-vX.Y.Z.bin` and `manifest.jws`, a **JSON Web Signature** (RFC 7515,
compact serialisation) whose payload is the manifest:

```text
BASE64URL(header) . BASE64URL(payload) . BASE64URL(signature)

header  {"alg":"ES256","typ":"sarathi-manifest+json","kid":"sarathi-ota-1"}
payload {
  "schema": 1,
  "board": "crowpanel21",
  "version": "0.3.0",
  "seq": 300,
  "size": 930480,
  "sha256": "9f2c…64 hex chars…",
  "min_protocol": 1,
  "url": "https://github.com/D4rth-C0d3r/sarathi/releases/download/v0.3.0/sarathi-crowpanel21-v0.3.0.bin"
}
```

- **The signature covers the whole manifest**, so every field is authenticated: the board
  (SUIT's class identifier), the version and sequence number (anti-rollback), the image
  size, its SHA-256 digest and the URL. The image is trusted through the digest. This is
  the SUIT / Azure Device Update model, using JSON and JOSE instead of SUIT's CBOR and COSE,
  because the rest of the system (BLE protocol, tools) already speaks JSON.
- **ES256** means ECDSA on P-256 with SHA-256 (RFC 7518, FIPS 186-5): a 64-byte signature,
  fast to verify, supported by the shipped mbedTLS.
- **`kid`** names the signing key, so a key can be rotated: the device holds a small table of
  trusted public keys and picks one by `kid`.
- **`seq`** is a monotonically increasing integer, derived from the version as
  `major*10000 + minor*100 + patch` (0.3.0 → 300). It is the anti-rollback counter: the
  device accepts only a `seq` greater than its own.
- **Where the device looks:** it fetches the manifest from the stable "latest" URL
  `https://github.com/D4rth-C0d3r/sarathi/releases/latest/download/manifest.jws`. This
  needs no GitHub API calls, so there's no rate limit and no token. Pre-releases are never
  "latest", so they never reach riders.
- The manifest format is specified in `docs/ota/` (description, JSON Schema for the payload,
  golden test vectors) before any firmware or CI code uses it, like the BLE protocol.
- **`min_protocol`** is the lowest phone-protocol version this firmware works with. The
  device shows "Update the phone app" when the connected app reports a lower version. It is
  advisory and never blocks the update.

**HTTP client.** The device uses ESP-IDF's `esp_http_client` (`sarathi_ota/https_get`), not
Arduino's `HTTPClient`. `HTTPClient`'s API is built on Arduino `String`, which CLAUDE.md keeps
out of our libraries, and `esp_http_client` is Espressif's standard client, with the
certificate bundle built in.

**Certificate validation (`esp_crt_bundle`).** Connections set `crt_bundle_attach =
esp_crt_bundle_attach`, so servers are checked against the root CA bundle compiled into the
firmware:

- The bundle holds the Mozilla root store, compressed and sorted by subject name.
  `esp_crt_bundle_attach()` installs a verify callback in mbedTLS.
- During the handshake the server sends its certificate chain. mbedTLS verifies the chain up
  to its last certificate. The callback then looks up that certificate's issuer in the
  bundle by subject name (binary search) and checks its signature with that root's public
  key. If no root matches, or the signature fails, the handshake fails.
- mbedTLS also checks that the certificate matches the host name sent in SNI. ESP-TLS sets
  that host name from the URL, again on every redirect hop.
- The bundle is the one the core already links (about 69 KB of flash). Certificate checks
  are never skipped.
- **Limitation:** the pinned core doesn't check certificate dates (`HAVE_TIME_DATE` off), so
  an expired certificate would still be accepted. That's acceptable because TLS is not
  what decides acceptance: the manifest signature (3) is. TLS gives privacy and makes tampering
  harder. Turning date checks on needs a rebuilt `sdkconfig` and a clock (SNTP); it's
  noted as a follow-up.

**GitHub's redirect.** Both URLs redirect:

- `…/releases/latest/download/manifest.jws` → `…/releases/download/vX.Y.Z/manifest.jws`
  (same host)
- the asset URL → a short-lived signed URL on GitHub's asset host (currently
  `release-assets.githubusercontent.com`, formerly `objects.githubusercontent.com`)

The device follows them **by hand** (`disable_auto_redirect`), so each `Location` header is
checked before it's used. Each hop is a new TLS connection, verified against the bundle for
its own host name. Rules:

- **HTTPS only**: a redirect to `http://` (or to `//host`) aborts. A relative path stays on
  the current https host.
- **At most 3 hops.**
- **No hard-coded asset host**: GitHub has changed it before.
- **Download straight after resolving**: the signed URL expires after a few minutes.

### 2. Wi-Fi credentials: captive portal first, BLE later

**First-time setup (Phase 2).** Settings → "Wi-Fi setup", available only when stopped like
all settings:

1. The device starts a soft AP named **`sarathi-setup`**. Its WPA2 password is 8 random
   characters generated per session, shown **on the device screen** together with the
   AP name. So only someone looking at the dash can join; the AP isn't open.
2. A DNS server answers every name with the AP's address (192.168.4.1), so phones show
   their "sign in to network" captive-portal sheet. A small HTTP page (one form: network
   name, password; plus a list of scanned networks) posts the credentials.
3. The device tries to join. It shows "Connected" or "Couldn't join" on screen, with the
   portal page reporting the same. On success it saves the credentials and stops the AP.
   The AP also stops after 5 minutes or on a long press.

**Storage.** NVS namespace `wifi`, key `cfg`: one versioned blob (SSID ≤ 32 B,
password ≤ 64 B), written once per successful setup. This keeps the settings blob small
and debounce-free. The Wi-Fi driver's own credential store is disabled
(`WiFi.persistent(false)`), so the password lives in one place only.

- NVS is **not encrypted**: that needs flash encryption, which burns eFuses irreversibly.
  Someone with physical access and a USB cable can read the Wi-Fi password. That risk is
  accepted and documented in `SECURITY.md`. "Forget Wi-Fi" in Settings erases it.

**Where the code lives.** Wi-Fi connection and the captive portal go in `sarathi_ota`, with
the parsing and validation logic in `sarathi_core`. ADR-0001 keeps Wi-Fi for OTA only, so
it gets no library of its own. The repo map already describes `sarathi_ota` as "Wi-Fi + BLE
OTA".

**Later (Phase 4).** The Android app sends credentials over the bonded BLE link as a new
protocol message (defined in `docs/protocol/` first, as always). The portal stays as the
fallback for riders without the app.

### 3. Integrity: verified before switching partitions

**On the device** (`sarathi_ota`; parsing and checks in `sarathi_core`, natively tested):

1. **Verify the manifest.** Split the JWS and require `alg` `ES256`, a known `kid` and the
   expected `typ`. Verify the signature over `header.payload` with that key (mbedTLS).
   Nothing in the payload is read before this passes.
2. **Check the payload.** Require the expected `schema` and `board`, a `size` that fits the
   inactive slot, a `seq` greater than the running image's, and an `https://` URL.
3. **Download.** Stream the image into the **inactive** slot with `esp_ota_begin` /
   `esp_ota_write`, hashing each chunk with SHA-256 as it arrives. Sequential-write mode
   erases each sector just before writing it, instead of stalling to erase the whole slot.
   Abort as soon as more bytes than `size` arrive.
4. **Check the image.** Require exactly `size` bytes and a SHA-256 equal to the manifest's
   digest. The image's own `esp_app_desc_t` version can't serve as an extra check: with
   the pinned core it holds the precompiled libraries' build id (`14a0af9`,
   `arduino-lib-builder`), not ours (verified). The version is authenticated by the signed
   manifest instead, and the running firmware knows its own from compile-time
   `SARATHI_VERSION` / `SARATHI_SEQ`.
5. **Switch.** Only then call `esp_ota_end()`, which validates the image format, then
   `esp_ota_set_boot_partition()`, then reboot. On any failure, call `esp_ota_abort()`.
   The boot partition never changes, and the screen says why.

**Why a detached, signed manifest rather than a signature inside the image:**

- The release image stays byte-identical to the build, so it can still be flashed over USB.
- One signature also authenticates the version, board and URL.
- It is the shape used by SUIT, Azure Device Update and AWS IoT OTA.
- Arduino's `UPDATE_SIGN` appends a 512-byte padded signature to the image and
  authenticates nothing else, so it isn't used.

**Keys.**

- **Generated by the maintainer**, never by tools or agents in this repo:
  `openssl ecparam -name prime256v1 -genkey -noout -out ota-signing.pem`. Nothing in this
  repo generates, prints or stores a real key.
- **Private key:** stored as the GitHub Actions secret `OTA_SIGNING_KEY` (PEM), in a
  protected **`release` environment**, then deleted locally, or kept offline if the
  maintainer wants a backup.
- **Release workflow:** `release.yml` is triggered by a `v*` tag and is the only job that
  runs in that environment. It builds the image and computes its size and SHA-256, writes
  the payload, and signs it with `tools/ota_manifest.py sign` (Python `cryptography` package,
  ES256 with the raw 64-byte r‖s signature that JWS requires). It then uploads the image and
  `manifest.jws`.
  - The key goes into a temporary file with `umask 077` and is deleted in an `always()` step.
  - Pull-request workflows never see secrets (GitHub's default for forks; we never use
    `pull_request_target`).
- **Public keys:** committed in `firmware/lib/sarathi_ota/ota_keys.h` as a table of
  `{kid, public key}`. To rotate, release N ships the old and the new key, and release N+1
  is signed with the new key's `kid`.
- **Local builds** (`pio run`) aren't signed. They can be flashed over USB as today but are
  never offered over OTA.
- **Tests** use a separate test key pair, committed under `docs/ota/vectors/` and labelled
  test-only. Its `kid` (`test-only`) is not in the firmware's key table.

**Hardware root of trust (not enabled).** A production product would also enable ESP32-S3
**Secure Boot V2** (the bootloader verifies every image with RSA-PSS or ECDSA against
eFuse-held key digests) and **flash encryption** (protects NVS secrets such as the Wi-Fi
password). Both are off in the shipped `sdkconfig`. Turning them on:

- **Irreversible:** burns eFuses on each board, and losing the key bricks the device.
- **Toolchain:** needs the rebuilt libraries and bootloader described in 4.
- **Openness:** stops riders from flashing their own builds, which matters for an open
  project.

So it is left as an opt-in hardening step for whoever builds devices, to be documented in
`SECURITY.md`. The manifest scheme above is unaffected by it.

### 4. Rollback

The bootloader's rollback flow, already present in the pinned build:

1. `esp_ota_set_boot_partition()` marks the new slot `ESP_OTA_IMG_NEW` in `otadata`, and the
   device reboots.
2. The bootloader boots it and sets it to `ESP_OTA_IMG_PENDING_VERIFY`.
3. If the app calls `esp_ota_mark_app_valid_cancel_rollback()`, it becomes `VALID`.
4. If the device resets **before** that, for any reason (crash, panic, watchdog,
   brown-out, or the rider power-cycling a frozen screen), the bootloader marks the slot
   `ESP_OTA_IMG_ABORTED` and boots the previous one.

**Firmware work:**

- Override `verifyRollbackLater()` to return `true` (in `sarathi_ota/boot_confirm.cpp`), so
  `initArduino()` doesn't mark the image valid before our checks. The core is C, so the
  override needs `extern "C"`. It also has to sit in an object file `main.cpp` references, or
  the linker drops it from the library archive and Arduino's weak default wins. Both were
  checked in the linked firmware: the symbol comes from `boot_confirm.cpp.o` and returns
  `true`.
- The task watchdog stays on, so a hang becomes a reset, and that reset becomes a rollback.
- After a rollback, the device shows an "Update failed, previous version restored" notice:
  `esp_ota_get_last_invalid_partition()` reports the aborted slot.

**If rollback had been off** (for reference, since it isn't):

- Enabling it with pioarduino means setting `custom_sdkconfig =
  CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in `platformio.ini`. pioarduino then rebuilds
  the ESP-IDF libraries and the bootloader from source (Arduino as an ESP-IDF component).
- The cost: a first build of about 10–20 minutes and a much larger CI cache, plus drift
  from Elecrow's tested binaries, which would need re-testing on the device. The bootloader
  is only flashed over USB, never over OTA, so every device would also need one cable flash.

### 5. When the app marks itself valid

`sarathi_ota::ConfirmBootIfHealthy()` runs from the UI task and marks the running image
valid only when all of these hold:

1. **UI up:** the display, LVGL and the shell have initialised, and the first frame has
   been flushed to the panel.
2. **BLE up:** `sarathi_link` has started advertising. BLE arrives in Phase 3; until
   then this condition is skipped, with a `TODO(Phase 3)` at the call site.
3. **Settled:** 10 s of uptime have passed with the UI loop running. This catches crashes
   a few seconds into a ride, for example in the first route redraw.

Wi-Fi is deliberately **not** a condition: an update that can't reach Wi-Fi must still be
kept, or a rider out of range would roll back every time. The boot log reports
`ota: image confirmed` or `ota: pending verify (Ns left)`.

### 6. Rider-facing behaviour

- **Manual only:** checking and installing happen from Settings ("Check for update"), so
  only when stopped. There are no automatic background downloads.
- **Confirm first:** the device shows the new version and size and asks for a knob click.
- **While updating:** the screen is static ("Updating… keep the bike stopped") with
  progress redrawn at most once per percent.
- **Flash writes and the panel** (found in the step 7 device tests). On this board every flash
  write garbles the RGB panel (see `docs/hardware.md`), so a rider must never watch one:
  - **Download:** the image goes into PSRAM first, so there are no flash writes while
    progress shows. An image too big for PSRAM streams to flash as before.
  - **Install:** the screen says "Installing… the screen goes dark for a few seconds", and the
    backlight turns off after `kInstallDarkMs`. The worker waits that long, then writes the
    whole image and restarts.
  - **Confirmation:** the new image confirms itself with the backlight off, then restarts once,
    because that write can leave the panel shifted until a restart. This happens only on the
    first boot after an update.
  - **Follow-up (not done):** fix the cause with a rebuilt `sdkconfig`
    (`CONFIG_SPIRAM_XIP_FROM_PSRAM`, `CONFIG_LCD_RGB_ISR_IRAM_SAFE`), in its own ADR and PR,
    tested on the device.
- **Cancel:** a long press cancels the download. Nothing changes until step 3.5 above.
- **Concurrency:** the download runs in its own task, which posts progress to the UI task
  through a queue. LVGL stays on the UI task.
- **Memory:** Wi-Fi plus one TLS session should need roughly 50–60 KB of internal RAM (an
  estimate, to be measured), and we have 213 KB free. Wi-Fi is turned off after the update
  or setup.

### 7. CI size budget

- The firmware workflow fails if `firmware.bin` exceeds **90 % of the app slot**:
  5,662,310 B of 6,291,456 B. The slot size is read from `partitions.csv`, not hard-coded.
- It writes the size and percentage to the job summary on every run.
- The margin leaves room for growth and for the root CA bundle (about 69 KB) and the Wi-Fi
  and TLS code that this phase adds.

## Alternatives considered

1. **GitHub REST API (`/releases/latest`).** Gives richer metadata but is rate-limited to
   60 unauthenticated requests per hour per IP, often shared behind carrier NAT, and
   returns large JSON. The `latest/download` URL avoids both.
2. **Own update server.** There is no backend to run. Rejected.
3. **Full SUIT (RFC 9124, CBOR + COSE).** The standard manifest format, but it needs a CBOR
   and COSE stack on the device and in CI, and its command sequences are far more than one
   image needs. JWS keeps the SUIT information model with tooling we already have.
4. **Signature over the image only** (the first draft, and Arduino's `UPDATE_SIGN`). It
   leaves version, board and URL unauthenticated, and `UPDATE_SIGN` also changes the image.
5. **Arduino `HttpsOTAUpdate` / `esp_https_ota`.** Simple, but neither verifies our
   manifest before switching partitions. We'd still need a hook between download and
   switch, so we use `esp_http_client` and `esp_ota_*` directly.
6. **Secure Boot V2 plus flash encryption.** The right hardware root of trust for a closed
   product. Here it is irreversible, needs a rebuilt toolchain and locks riders out of their
   own builds, so it is opt-in later (see 3).
7. **Wi-Fi credentials typed with the knob.** Too slow and error-prone. The captive portal
   is quicker, and BLE provisioning follows in Phase 4.
8. **Open setup AP.** Anyone nearby could set the device's Wi-Fi. The per-session password
   shown on the dash requires being at the bike.

## Consequences

**Good**

- A failed or crashing update rolls back by itself, using what the pinned core already has.
- No server, token or API limit. Releases are ordinary GitHub releases plus one manifest.
- Only manifests signed by CI's key are accepted, only for this board, and only for
  newer versions. That meets EN 303 645 5.3 and the SP 800-193 protection, detection and
  recovery properties without irreversible eFuse changes.

**Costs and follow-ups**

- About 69 KB of flash for the root CA bundle, plus the Wi-Fi, HTTP and TLS code. That's
  measured when the client lands and kept in check by the CI budget.
- A JSON parser is needed on the device; the BLE protocol needs one anyway. Choosing it
  (for example ArduinoJson, header-only, also builds natively) is a dependency decision
  for the implementation step.
- Certificate dates aren't checked by the pinned mbedTLS. This is mitigated by the
  signature; a follow-up is to enable `HAVE_TIME_DATE` plus SNTP if the toolchain is ever
  rebuilt.
- Wi-Fi credentials sit unencrypted in NVS.
- The maintainer must create the signing key and the `OTA_SIGNING_KEY` secret before the
  first signed release. Losing the key means devices need one USB flash with a new public
  key.
- BLE readiness can only join the "mark valid" condition when `sarathi_link` exists
  (Phase 3).
- The bootloader itself is never updated over the air.

## Implementation plan (Phase 2)

1. This ADR and the CI size check.
2. `docs/ota/manifest.md`, `docs/ota/manifest.schema.json` and `docs/ota/vectors/`
   (signed and tampered golden manifests, plus a test-only key pair that release builds
   never trust).
3. Core logic in `sarathi_core`, with native tests: JWS splitting and base64url, manifest
   checks, `seq` comparison, the confirm-boot decision, and the update state machine. ES256
   verification itself stays in `sarathi_ota` (mbedTLS), tested on the device against the
   same vectors.
4. `sarathi_ota`: rollback hook and boot confirmation, Wi-Fi connect, HTTPS fetch with the
   bundle and redirects, streaming download with hash and signature check.
5. Captive-portal provisioning and Settings rows ("Wi-Fi setup", "Check for update",
   "Forget Wi-Fi"), plus the static update screen.
6. `release.yml` using `tools/ota_manifest.py sign`: build, hash, sign, publish. This needs the
   maintainer's `OTA_SIGNING_KEY` secret in the `release` environment.
7. Device tests: a normal update; a tampered manifest or image (rejected, no partition
   switch); a downgrade (rejected); and a deliberately crashing image (rolled back).
