# ADR-0015: Firmware updates from the app over BLE

- **Status:** Accepted (2026-11-14)
- **Date:** 2026-11-14
- **Related:**
  - ADR-0004 (Wi-Fi OTA and rollback)
  - ADR-0007 (protocol v1: OTA reserved)
  - ADR-0010 (the ride link)
  - `docs/ota/manifest.md`

## Context

Today the dash updates over Wi-Fi from GitHub Releases (ADR-0004):

- **Signing:** a JWS-signed (ES256) manifest; the device verifies the image's SHA-256 and its
  board, and checks that `seq` is newer.
- **Install:** the image is downloaded to **PSRAM** and installed with the screen dark.
- **Rollback:** the bootloader keeps the previous image, and a boot self-check confirms the new
  one.

On a bike there's no Wi-Fi, and the phone is already connected. Protocol v1 reserves message
types `0x60`–`0x6F` for OTA over BLE.

The owner's constraints:

- **Signed images**, verified on the device before switching. Reuse the Phase 2 rollback.
- **No secure boot, no flash encryption, no eFuses burned**, ever. These are irreversible. Ask the
  owner before anything that touches eFuses.
- **Resumable** transfers.
- **A compatibility check** between app, protocol and firmware.
- **No updates while navigating.**

## Decision

### 1. The same signed manifest and image, carried by the phone

- **Source:** the app fetches `manifest.jws` and the image from the same GitHub Release the
  Wi-Fi path uses.
- **Delivery:** the app sends both to the dash.
- **Verification:** the device runs the existing manifest verification (`docs/ota/manifest.md`
  steps 1–6) with the keys built into the firmware.
- **Unsigned images:** never accepted over BLE. Development builds are flashed over USB.
- **Signature:** application-level ECDSA, as today. **No secure boot, no flash encryption, no
  eFuse writes:** nothing in this design touches eFuses, and any later proposal that would comes
  to the owner first.

### 2. Into PSRAM, then the Phase 2 install

- **Receiving:** the image is received into a PSRAM buffer, as the Wi-Fi path downloads it. While
  it arrives, nothing is written to flash, so the panel isn't disturbed (`docs/hardware.md`,
  "Flash writes disturb the panel"). The dash shows the transfer's progress, and the ride UI
  stays usable.
- **Fit:** the phone build's image is about 2.0 MB (2026-11-14). PSRAM has about 5.4 MB free.
  An image larger than the free PSRAM is refused with `too_large`; CI already caps images at 90 %
  of the 6 MB slot.
- **Installing:** once the whole image is in and its SHA-256 matches, the **unchanged Phase 2
  path** installs it:
  - the backlight goes off
  - the image is written to the inactive slot
  - the slot is set to boot and the dash restarts
  - the self-check confirms the image once the UI and BLE are up, or the bootloader rolls back

### 3. Resumable

- **On a drop:** if the link drops, the device keeps the buffer and the received ranges for
  **10 minutes** (or until a restart: PSRAM doesn't survive one).
- **On reconnect:** the app sends `ota_begin` again with the same image SHA-256. The device
  answers with the offset it has, and the transfer continues from there.
- **Resuming after a power-off:** a non-goal. It would mean writing a partial image to flash
  during the transfer, which shifts the panel.

### 4. Compatibility

- **On the phone, before offering an update:**
  - The `board` matches the dash's (from `hello`).
  - `seq` is newer than the dash's.
  - The firmware's protocol major is one this app speaks. If not, the app first asks the user to
    update the app.
  - **Manifest change, flagged:** the manifest gains a `protocol` member (for example `"1.4"`), the
    firmware's own protocol version. `min_protocol` already exists but is advisory. The manifest
    format is updated in `docs/ota/` first, with new golden vectors.
- **On the device:** the manifest checks (board, newer `seq`, signature). It also checks that it
  isn't navigating, in a call or moving.
- **A table in each release** lists which app versions work with which firmware (ADR-0019).

### 5. Never while navigating

- **On the device:** `ota_begin` is refused with `busy` while navigation is active, a call is on,
  or the moving detector says moving. That includes rides where the rider turned the moving lock
  off: the update rule doesn't follow that switch.
- **In the app:** it offers the update only with no ride running, and asks the rider to stay near
  the dash with it powered.
- **If a ride starts mid-transfer,** the device aborts and drops the buffer.

### 6. Protocol (spec first, `docs/protocol/`)

- **New messages:** in the reserved `0x60`–`0x6F` range, on the existing `rx`/`tx`
  characteristics, with no new characteristic.
  - `ota_begin` (JSON): the manifest JWS, and the SHA-256 again.
  - `ota_chunk` (binary): offset and bytes.
  - `ota_status` (device → phone): offset held, or an error name from the manifest table, plus
    `busy`, `too_large`, `aborted`.
  - `ota_end`: start the install.
- **Flow control:** the existing per-frame credits.
- **A protocol minor version,** numbered when implemented.

### 7. Transfer time at MTU 247

**Payload per write:** a write without response carries up to 244 bytes. After our frame header
that's about **236 bytes of image**.

**Speeds:**
- **Theoretical:** at Android's high connection priority (11.25–15 ms connection interval), with
  4–6 writes per interval, that's 60–125 KB/s.
- **Earlier assumption:** ADR-0009 assumed 8 KB/s for a background link with the phone in a
  pocket.
- **For updates:** the app requests high priority, since the rider has it open.

| Image | 8 KB/s (pocket, worst) | 20 KB/s (expected) | 40 KB/s (good) |
|---|---|---|---|
| 2.0 MB (today) | 4 min 15 s | 1 min 40 s | 50 s |
| 5.4 MB (the 90 % cap) | 11 min | 4 min 30 s | 2 min 15 s |

Install and restart add about 20–30 s.

**Measurement:** the real speed is measured in the first implementation step (bytes per second in
the log), and this table is corrected.

**Optional later:** sending the image deflate-compressed and inflating on the device, with the
ESP32-S3 ROM's `tinfl`, would cut transfer time by roughly a third to a half. It's not part of
the first version.

## Consequences

- **The Wi-Fi path stays.** BLE is an addition, sharing the verification, install and rollback
  code.
- **SECURITY.md** gains the BLE update path. The threat model stays as ADR-0004's: a signed
  manifest, no downgrade, the bonded link required.
- **On-device tests** in `docs/phase5-test-plan.md`:
  - a drop mid-transfer and a resume
  - a refusal while navigating
  - a wrong board
  - an old `seq`
  - a bad hash
  - rollback on a self-check failure
  - an incompatible protocol
