# ADR-0017: Power, display brightness and heat

- **Status:** Accepted (2026-11-14)
- **Date:** 2026-11-14
- **Related:**
  - ADR-0003 (themes)
  - ADR-0007 (protocol: time)
  - ADR-0009 (renderer)
  - ADR-0016 (brownouts)
  - `core/display_policy.h`
  - `core/ride_health.h`
  - `docs/hardware.md`

## Context

The dash is powered from the bike's USB socket.

### What the board has

From Elecrow's schematic, `vendor/crowpanel-2.1/Eagle_SCH&PCB`, read 2026-11-14:

- **Power:** 5 V in, through a DC-DC converter, to 3.3 V. There's a P-MOSFET switching the panel's
  supply (PCF8574 P3) and a power LED.
- **No battery, no charger, no supercapacitor:** when the 5 V goes, the dash goes off at once.
- **No ambient light sensor.**
- **No way to measure the input voltage was found:** no divider to an ADC pin. This is to confirm
  on the board, but the design below doesn't rely on one.

### Today

- **Brightness:** the brightness setting, with auto day/night switching at fixed times (19:00 to
  07:00, `display_policy.h`).
- **Heat:** the heat cap (step H) limits the backlight to 50 % at 80 °C and lifts it below 72 °C.
- **Boot and reconnect:** a cold boot shows the full screen in about 2 s (Phase 3 case 13), and a
  bonded phone reconnects within about 3 s (D6).

## Decision

### 1. Power

- **Power loss is a normal off.** With no stored energy, the dash can't "sleep when power drops":
  it just stops.
  - **Safe writes:** everything it writes must survive that: NVS commits are atomic, and settings
    saves are debounced.
  - **No shutdown screen:** none is possible, and none is needed.
- **Brownouts:** cranking the engine can sag the 5 V supply.
  - **Counted, not crashes:** brownout resets are counted (ADR-0016), not treated as crashes.
  - **Logged:** the boot logs `power: brownout reset`.
  - **Wiring advice:** `docs/hardware.md` gains advice to use a socket switched by the ignition
    (accessory power), or an input filter. That's for Phase 6, bike integration.
- **Standby on always-on sockets:** some sockets stay live with the bike off. After **no BLE link
  for N minutes** (default 10; settable 5–60, or never) and no knob input:
  - the backlight goes off, and the panel's supply goes off (PCF8574 P3)
  - LVGL stops refreshing, and the CPU drops to 80 MHz
  - BLE keeps advertising at a slow interval (about 1 s)
  - **Wake:** on any knob turn or press, or when the bonded phone connects.
  - **Timing:** the panel back on takes about 0.5 s; the phone's `autoConnect` finds the slow
    advertising within a few seconds.
  - **Deep sleep is not used:** it would stop BLE, so only the knob could wake the dash.
  - **Drain:** the standby current is measured with a USB meter in the first step, to say how
    many days a bike battery can feed it.

### 2. Brightness without a light sensor

- **Sun times from the phone:** the phone computes **sunrise and sunset** for its own position,
  with the NOAA algorithm, offline. It sends them in `time` as **minutes of the local day**.
  - **No position over the link:** positions never reach the device as latitude or longitude
    (CLAUDE.md), and these times don't reveal one.
  - **Protocol:** optional fields in `time`, a minor version, spec first.
- **The dash's day/night:**
  - **Day and night:** "night" runs from sunset to sunrise, replacing the fixed 19:00 to 07:00.
  - **Fallback:** without sun times (an older app, or no fix yet), the fixed hours stay.
  - **Twilight:** a 20-minute ramp at each end, in 5 steps, never a jump while riding.
- **Levels:**
  - **Day:** the brightness setting, defaulting to 100 % for sunlight.
  - **Night:** 40 % of it (today's rule), with a separate "night brightness" setting.
  - **Floor:** never below the 5 % floor.
- **Tunnels:** without a sensor, nothing changes in a tunnel. That's accepted.
- **Later hardware option:** a light sensor on the free I2C bus (the touch and PCF8574 bus) is a
  Phase 6 option, with its own ADR.
- **The phone's own light sensor isn't used:** it's in a pocket.

### 3. Heat: degrade gracefully before any shutdown

The steps go from the step H cap (80 °C) upwards. Each step has hysteresis (cool 6 °C below it to
step back) and is logged.

| Level | Die temperature | What changes |
|---|---|---|
| Normal | below 75 °C | Nothing |
| Warm | 75 °C | Map redraw capped at 10 fps (from about 18); theme ornaments static |
| Hot | 80 °C (today's cap) | Plus backlight capped at 50 % |
| Very hot | 90 °C | Plus the map off: arrow mode with the next turn and distance only; backlight 30 % |
| Critical | 100 °C | Plus a static "Too hot: navigation continues on the phone" screen; backlight 10 %; LVGL refresh 1 fps; BLE stays up |
| Off | the chip's rated maximum (to take from the ESP32-S3 datasheet) | The firmware turns the panel supply off and waits, rechecking every 30 s; it never turns itself off for good |

- **Thresholds are provisional:** they come from H11's readings (an hour in the sun), which are
  still pending. They're set in `sarathi_core` with native tests.
- **The rider sees it:** a small "Hot" note on Nav from Very hot onwards, never colour alone
  (ADR-0003). Settings' health row shows the level.
- **The phone isn't asked to do anything:** guidance continues there as always.

## Consequences

- **Protocol:** additions for sun times, spec first.
- **Settings:**
  - standby after N minutes
  - night brightness
- **Pure logic in `sarathi_core`, with native tests:** `ride_health` grows into heat levels, and
  `display_policy` takes sun times.
- **On-device tests:**
  - standby and wake by knob and by phone, with the standby current
  - a brownout count, with power cycled during a settings save
  - day/night at sun times
  - heat levels, with a debug-only override that injects a temperature
