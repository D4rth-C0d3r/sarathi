# sārathi

An open-source motorcycle navigation and dash companion.

In Sanskrit, a *sārathi* is a charioteer, as Krishna was Arjuna's.

> [!WARNING]
> **Safety disclaimer. Read before you ride.**
>
> - sārathi is experimental hobby software. It is **not** a certified automotive or safety
>   device. Use it at your own risk.
> - **You are responsible for riding safely.** Set up your route, music and settings before
>   you ride. While moving, glance at the display only. Don't operate the device or your
>   phone. If you need to change something, pull over and stop.
> - Navigation directions, caller ID and other data can be wrong, late or missing. Always
>   follow road signs, traffic rules and your own judgement.
> - Follow your local laws on handlebar-mounted displays and phone use while riding.
> - Mount the device securely, where it doesn't block your controls, lights or view of the
>   road. A device that comes loose at speed is a hazard to you and others.
> - The software is provided "as is", without warranty or liability, as set out in
>   sections 7 and 8 of the [Apache License 2.0](LICENSE).

## What it does

- Turn-by-turn navigation prompts
- Caller ID with accept/reject
- Music control with track info
- Settings, operable with the knob alone (gloves on)
- Firmware updates over Wi-Fi and Bluetooth LE

## How it works

The device has Bluetooth LE only (no Classic Bluetooth), so **the phone is the brain and the
device is a BLE display and remote**. Call and music audio stay on your helmet intercom.
An Android companion app sends navigation, call and media data to the device over a custom
GATT service. iOS is out of scope.

## Hardware

[Elecrow CrowPanel 2.1" Rotary Display](https://www.elecrow.com/wiki/CrowPanel_2.1inch-HMI_ESP32_Rotary_Display_480_IPS_Round_Touch_Knob_Screen.html)
(DHE03921D): ESP32-S3, 480×480 round IPS touch screen, rotary knob with push button.

## Status

**Phase 0: bring-up.** Nothing is usable on a bike yet.

| Phase | Scope |
|---|---|
| 0 | Bring-up |
| 1 | UI shell, settings, simulator |
| 2 | Wi-Fi OTA with rollback |
| 3 | ChronosESP32 quick win |
| 4 | Own Android app and protocol |
| 5 | BLE OTA |
| 6 | Bike integration |
| 7 | Extras: map snapshots, GPS, trip stats |

## Repository layout

```
firmware/   PlatformIO project for the ESP32-S3
android/    Android companion app (Kotlin, Compose)
docs/       protocol, architecture decisions (ADRs), build guide
hardware/   enclosure and wiring
tools/      setup scripts and PC-side helpers
vendor/     Elecrow's examples (git submodule, read-only)
```

## Getting started

```bash
git clone --recurse-submodules --shallow-submodules https://github.com/D4rth-C0d3r/sarathi.git
cd sarathi
tools/setup.sh .
```

Full build instructions will live in `docs/building.md`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) and the [Code of Conduct](CODE_OF_CONDUCT.md).
To report a security issue, see [SECURITY.md](SECURITY.md).

## License

[Apache License 2.0](LICENSE).

sārathi is an independent project. It is not affiliated with or endorsed by Elecrow.
