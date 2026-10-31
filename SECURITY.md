# Security policy

## Supported versions

sārathi is pre-release. Only the latest release (or `main`, until the first release)
receives security fixes.

## Reporting a vulnerability

**Please don't report security issues in public issues, discussions or pull requests.**

Report them privately through GitHub instead:

1. Go to the repository's **Security** tab.
2. Choose **Report a vulnerability**.
3. Describe the issue, how to reproduce it, and its impact.

See GitHub's guide to
[privately reporting a security vulnerability](https://docs.github.com/en/code-security/security-advisories/guidance-on-reporting-and-writing-information-about-vulnerabilities/privately-reporting-a-security-vulnerability).

This is a hobby project maintained in spare time. You'll get an acknowledgement as soon as
possible, and fixes are prioritised by severity. Please give us a reasonable time to release
a fix before disclosing the issue publicly.

## Scope

Issues we especially want to hear about:

- BLE pairing and bonding, including control or OTA writes accepted without a bonded link
- Firmware updates: signature checks, rollback, or installing unsigned images
- The Android app's handling of notifications, contacts and Bluetooth permissions
- Leaked secrets in the repository or CI

Out of scope: attacks that need physical access to an unlocked phone, or that require
opening the device and reflashing it over USB.

## Release signing key

Firmware updates are accepted only if their manifest is signed with the project's ECDSA P-256
key (ES256, ADR-0004). The **private** key exists in exactly one place: the GitHub Actions
secret `OTA_SIGNING_KEY` in the protected `release` environment, which only `v*` tags can
use. It is never committed, never printed, and never kept on a laptop. Tools in this repo
never generate a real key.

### Creating the key (maintainer, once)

Run this yourself, in a terminal, from the repository root. It needs `openssl`, plus the
`gh` CLI logged in with admin rights on the repository. The private key goes from `openssl`
into the secret through a private temporary folder that is shredded afterwards; only the
public key is kept.

```bash
( set -euo pipefail
  umask 077
  T=$(mktemp -d)
  trap 'shred -u "$T"/* 2>/dev/null; rm -rf "$T"' EXIT
  openssl ecparam -name prime256v1 -genkey -noout -out "$T/priv.pem"
  gh secret set OTA_SIGNING_KEY --env release --repo D4rth-C0d3r/sarathi < "$T/priv.pem"
  umask 022
  openssl ec -in "$T/priv.pem" -pubout -out docs/ota/keys/sarathi-ota-1.pub.pem
  openssl pkey -pubin -in docs/ota/keys/sarathi-ota-1.pub.pem -outform DER | sha256sum )
```

Then:

1. Commit the public key.
2. Record its fingerprint in `docs/ota/keys/README.md`.
3. Add it to `firmware/lib/sarathi_core/ota_trusted_keys.h`; a native test checks the two
   match.

The `release` environment should allow deployments from `v*` tags only (Settings →
Environments).

There is deliberately **no backup**. Losing the key means making a new one and shipping a
release that trusts it, which devices that trust only the lost key can't install over the air.

### Rotating, losing or leaking the key

- **Rotate:** see `docs/ota/keys/README.md`. In short:
  - Create the next key in a second secret, `OTA_SIGNING_KEY_NEXT`, with the commands above
    (new `kid`).
  - Ship a release that trusts both keys, signed with the old one.
  - Then sign with the new key and delete the old secret.
- **Lost:** create a new key as above. Devices still trusting only the lost key need one USB
  flash of a build that trusts the new one.
- **Leaked (or suspected):** move off the old key as fast as possible, in this order:
  1. Create the new key in `OTA_SIGNING_KEY_NEXT`, with a new `kid`.
  2. Ship a release that trusts **only** the new key. It has to be signed with the old key one
     last time, because that's the only key devices trust until they install it.
  3. Switch the release workflow to the new key, then delete the old secret.

  Until devices install that release, the leaked key can sign updates they would accept, so
  announce it and urge riders to update.

## Phone link (BLE)

There are two phone links, chosen at build time. Both pair the same way and listen only to a
paired phone.

### Our own link (protocol v1, `crowpanel21-phone`)

The link the sārathi app will use (ADR-0007, `docs/protocol/protocol.md`):

- **Pairing is confirmed on the dash.** LE Secure Connections with numeric comparison: the
  phone and the dash show the same 6-digit code, and the rider confirms with a knob press
  (focus starts on Cancel). A link without MITM protection ("Just Works") is never used.
- **Nothing moves before pairing.** Writes need an encrypted, authenticated link at the ATT
  level, and the device also refuses any write that doesn't come over an encrypted, bonded
  and authenticated link. An unpaired phone can turn notifications on (NimBLE doesn't extend
  the encryption requirement to that), but the device sends nothing until the link is
  secured, subscribed and its MTU raised.
- **One phone at a time.** A second phone that connects is disconnected.
- **Checked input.** Every frame and message is checked against the spec (lengths, CRC,
  ranges, UTF-8) before it's used; the same checks run against golden vectors in CI. Positions
  arrive only in the ride's local frame, never as latitude and longitude.
- **Paired phones reconnect silently.** Settings → **Forget phones** deletes every bond.
- **No updates over BLE** yet; OTA messages are reserved for Phase 5.

### The Android app (Phase 4)

Each permission and why (ADR-0010; the manifest says the same):

| Permission | Why |
|---|---|
| Nearby devices (`BLUETOOTH_CONNECT`) | Connect to the paired dash. There is **no** `BLUETOOTH_SCAN`: Android's companion chooser finds the dash. |
| Notifications | The ride link's ongoing notification, with Stop. |
| Location, and location all the time | Navigation needs the position during the ride, with the phone locked. Positions reach the dash only as distances in the ride's own frame, never latitude and longitude (ADR-0007). |
| Foreground service (connected device, location) | Keep the link and location running with the screen off. |
| Companion device (start in background, run in background, observe presence) | Restart the link when Android needs to, and optional auto-start. |
| Ignore battery optimisation | Asked for once, confirmed by the rider in a system dialog. |
| Notification access | Music: read the playing media sessions. Calls: the caller's name from the call notification, and an internet call's own Answer and Decline buttons. Nothing else is read, and nothing is kept. |
| Phone (`ANSWER_PHONE_CALLS`, `READ_PHONE_STATE`) | Answer or reject a call from the dash, and know when a call rings, is answered or ends. **No contacts or call-log permission**: the name comes from the call notification your phone already shows. |

The app talks only to the dash it paired, over the bonded, authenticated link described above.
Backup and device transfer are off: bonds and settings stay on the phone.

### The Chronos app (Phase 3, `crowpanel21-chronos`)

The interim link through the Chronos app (ADR-0005). The ChronosESP32 library it's built on
has no BLE security of its own, so sārathi adds it:

- **Pairing is confirmed on the dash.** When a new phone connects, the device asks it to
  pair with numeric comparison: the phone and the dash show the same 6-digit code, and the
  rider confirms with a knob press (focus starts on Cancel). A request times out after 30 s.
- **Only a paired phone is listened to.** Writes are accepted only over a link that is
  encrypted, bonded and authenticated (MITM-protected). A "Just Works" bond, which never
  showed a code, isn't enough.
- **Paired phones reconnect silently.** Settings → **Forget phones** deletes every bond
  (stored in NVS). Do that before giving a device away.
- **Our parser, not the library's.** The library's packet parser trusts lengths and indices
  sent over the air (it can write past its buffers). It never runs: our receiver checks
  every length, offset and index, and decodes only the packet types the dash uses. App
  notifications other than calls are ignored, never shown or stored.
- **No updates over BLE.** Firmware updates come only through the signed Wi-Fi path above.

## Known limitations (by design, for now)

- **Wi-Fi password at rest:** the saved Wi-Fi password is stored **unencrypted** in the
  device's NVS. Someone with the device and a USB cable can read it. Protecting it needs
  flash encryption, which burns eFuses irreversibly, so it's left to whoever builds devices
  (ADR-0004 §3). Use **Settings → Forget Wi-Fi** before giving a device away.
- **Setup network:** the setup network `sarathi-setup` uses WPA2 with a fresh 8-character
  password shown only on the dash. It runs only while setup is open, at most 5 minutes.
- **No hardware root of trust:** updates are accepted only if their manifest is signed by the
  project's key (ES256), but Secure Boot is not enabled. Anyone with physical access can still
  flash their own firmware over USB.
- **No certificate date checks:** the pinned toolchain doesn't check TLS certificate dates.
  Update integrity rests on the manifest signature, not on TLS.
- **Pairing prompts from strangers:** anyone in range can ask to pair, which shows the
  pairing screen. They can't pair without a knob press on the device, but they can make the
  prompt appear again after each Cancel. It's an annoyance, not access.
- **Data before encryption (Chronos build only):** in the second or two before a bonded
  phone's link is encrypted, the Chronos library may send its watch-info and battery
  notifications unencrypted. They carry nothing personal. Our own link (protocol v1) closes
  this: its characteristics require an authenticated, encrypted link, and the device sends
  nothing before that.
