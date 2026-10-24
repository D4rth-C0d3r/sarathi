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
