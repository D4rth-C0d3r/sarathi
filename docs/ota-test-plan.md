# OTA test plan (on the device)

The failure cases a Wi-Fi update must survive (ADR-0004). **Every case must end with a working
device.** Run them on the real board, record the result and date here, and re-run them when
OTA code changes.

## Setup

- **Device:** CrowPanel 2.1" on USB, with a Wi-Fi network saved (Settings → Wi-Fi).
- **Serial log** (115200), to confirm what the screen shows.
- **Test release:** a GitHub **pre-release** made by the release workflow from a tag
  `vX.Y.Z-rc.N`. A pre-release is never "latest", so riders never see it.
- **Test firmware:** a local build pointed at the pre-release's manifest instead of
  "latest". Build it from `firmware/` and flash over USB:

  ```bash
  PLATFORMIO_BUILD_FLAGS="'-DSARATHI_OTA_MANIFEST_URL=\"https://github.com/D4rth-C0d3r/sarathi/releases/download/<tag>/manifest.jws\"'" \
    pio run -e crowpanel21 -t upload
  ```

  A plain local build reports version `0.0.0-dev` (seq 0). To pretend to run another version,
  add `'-DSARATHI_VERSION=\"0.4.0\"' -DSARATHI_SEQ=400` to the same flags.
- **Hidden test option:** to fail the boot self-check on purpose, hold the knob button from
  power-on (or from "Installing…") until the confirmation point, 10 s after start. On a
  freshly updated image that rolls back at once. On any other image it only logs "nothing to
  roll back".

## Cases

| # | Case | How | Expected | Result |
|---|---|---|---|---|
| 1 | Normal update | Test firmware → Check for update → install | Clean progress; "Installing…", then dark; restarts into the new version; about 10 s later, dark and one more restart; Version shows the new one | **Pass**, 2026-10-24 (`rc.2`) |
| 2 | Power cut before confirmation | As 1, but unplug USB within 5 s of the new version starting | Boots the previous version; notice "Update failed. The previous version was restored." | **Pass**, 2026-10-24 (`rc.1`) |
| 3 | Power cut mid-download | Unplug USB at about 50 % | Boots the previous version: nothing was written to flash yet (the download goes to PSRAM); a later update works | Pending |
| 4 | Wi-Fi drop mid-download | Switch off the router (or hotspot) at about 50 % | "Update failed: The download stopped"; click to close; the device works normally | Pending |
| 5 | Corrupted image | Replace the pre-release's `.bin` with a copy with one byte flipped; the signed manifest is unchanged | "Update failed: The download was damaged"; the boot partition is unchanged; the original `.bin` is put back | Pending |
| 6 | Wrong signature | Test firmware pointed at `manifest-badsig.jws` (the manifest with one signature byte flipped), uploaded to the pre-release | "Update failed: The update didn't pass its security checks"; nothing downloaded | Pending |
| 7 | Image fails before marking valid | Install, then hold the knob button from "Installing…" through the restart until the screen goes dark again | Rolls back to the previous version at the confirmation point; the rollback notice shows | Pending |
| 8 | Same version | Test firmware built as 0.3.0 (seq 300), pointed at a 0.3.0 pre-release | "Up to date"; nothing downloaded | Pending |
| 9 | Older version (downgrade) | Test firmware built as 0.4.0 (seq 400), pointed at a 0.3.0 pre-release | "Up to date": the older release is refused (`not_newer`) | Pending |

Cases 5, 6, 8 and 9 rely on the same checks the 26 golden vectors cover (`docs/ota/vectors`).
On the device they also prove that the right message reaches the screen and that nothing is
written.

## Results log

- **2026-10-24:** cases 1 and 2 passed during Phase 2 step 7. Along the way:
  - "image confirmed" was invisible (ESP-IDF logging capped at ERROR); fixed.
  - Flaky connections to GitHub's file host; added retries and turned Wi-Fi power-save off.
  - Flash writes garbled the panel (documented in `docs/hardware.md`); the image now
    downloads to PSRAM and installs in the dark.
