# Phase 4 test plan

On-device checks for each Phase 4 step, run on the CrowPanel with the noted build. Native and
JVM tests cover the logic; these cover what only the hardware shows.

## Step C: our own phone link (`crowpanel21-phone`, `tools/ble_client.py`)

Flash with `pio run -e crowpanel21-phone -t upload` (from `firmware/`), keep
`pio device monitor` open, and run the fake phone from the repository root in the `.venv`.

| # | Check | How | Pass |
|---|---|---|---|
| C1 | Advertising | Boot; `python tools/ble_client.py scan` | The dash is listed; Nav shows "No phone"; Settings shows "Phone: not connected" |
| C2 | Pairing, confirmed | `ride`; when both show a code, turn to **Pair**, click, confirm on the computer | Same code on both; log: `link security: encrypted 1, bonded 1, authenticated 1`; "Forget phones (1)" |
| C3 | Pairing, refused | `forget`, then `ride` again and click **Cancel** on the dash | The computer reports a failed pairing; nothing reaches the dash; Nav shows "No phone" again |
| C4 | Reconnecting | Stop the tool (Ctrl-C), run `ride` again | No new code; connects at once |
| C5 | The ride | Let `ride` run | Nav map with the route, the turn, distance and ETA; the map turns at the corners; settings lock while moving and unlock at the two stops |
| C6 | Attribution, stopped | At the first stop (traffic light, 10 s) | Backend name and attribution in the status spot, small, inside the circle, in every theme (switch theme in Settings at the stop) |
| C7 | Backend switch | `ride --fallback-at 20` | "Now: Mappls" for 10 s while riding |
| C8 | Call | At 60 s ride time, Accept on the dash; then again with Reject (restart the tool) | The tool logs `call_action: accept` and the dash shows the active call; reject ends it |
| C9 | Music | On Music: play/pause, next, previous | The tool logs `media_cmd: ...`; title and time follow |
| C10 | Stale data | Pause the tool (Ctrl-Z) for 15 s, then `fg` | "Position stale Ns" after 3 s, "Navigation stale" after 10 s; both clear when it resumes |
| C11 | Disconnect | Ctrl-C mid-ride | Within a few seconds: "No phone"; nav and call cleared, not frozen |
| C12 | Wi-Fi pauses BLE | Settings → Check for update while connected | Log: `BLE stopped for Wi-Fi`; Nav shows "Bluetooth paused"; after it, the tool can reconnect |
| C13 | Forget phones | Settings → Forget phones | The tool disconnects; `ride` asks for a code again |
| C14 | Second phone | Scan with another BLE central (for example nRF Connect) while the tool is connected | The dash isn't listed: it stops advertising while a phone is connected. (If two connect at the same instant, the second is turned away: `extra phone(s) turned away`.) The ride carries on |

### Results, 2026-10-31 (the fake phone on the maintainer's laptop)

All of C1-C14 pass on the device, after the fixes the first run found:

- **Pairing through BlueZ** failed at first: the dash disconnected on its own failed security
  request before the laptop could pair, and GNOME's agent only answers with Settings →
  Bluetooth open. Fixed in the firmware and the tool; documented in `docs/building.md`.
- **The map redrew five times a second** (every pose), holding the dash at about 5 fps; now
  once a second.
- **"Phone not connected" was cut** to "Phone not..." on the Nav card; now "No phone".
- **"Securing link"** showed while a computer's Bluetooth settings held the secured link
  without the app; now "Waiting for app".
- **An unpaired central can turn notifications on** (NimBLE doesn't protect the CCCD); the
  device still sends nothing before the link is secured. SECURITY.md corrected.

The update check during C12 couldn't reach GitHub from the test network (TLS connect
timeouts); BLE paused and came back as it should.
