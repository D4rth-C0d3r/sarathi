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
