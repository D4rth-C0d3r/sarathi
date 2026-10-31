# ADR-0011: The moving lock from GPS speed, configurable and switchable

- **Status:** Accepted (2026-10-31)
- **Date:** 2026-10-31
- **Related:** ADR-0005 (the Chronos stand-in), ADR-0007 (protocol), ADR-0009 (ride frame),
  CLAUDE.md "Rider safety"

## Context

CLAUDE.md requires the dash's settings to lock while the bike moves. The demo ride locks at a
fixed 5 km/h; the Chronos build has no speed, so it locks while navigating instead (ADR-0005).
With our own app, the phone's GPS speed reaches the dash in `pose`. A bare threshold would let
GPS jitter at a standstill lock and unlock the settings.

The owner asked for the thresholds to be adjustable in the app, and for a switch that turns the
lock off.

## Decision

1. **Hysteresis on the dash** (`core::MovingDetector`): moving once speed stays at or above
   `move_kmh` for `move_s`; stopped once it stays below `stop_kmh` for `stop_s`. Defaults 5 km/h
   for 2 s and 3 km/h for 5 s.
2. **No speed, no guessing:** with no `pose` for 5 s, the dash falls back to the Chronos rule,
   locked while navigating.
3. **Set from the app:** an optional `lock` message (protocol 1.1, 0x0B) carries the thresholds
   and `enabled`; the app stores them and sends them on connect and on change. A 1.0 dash
   ignores it.
4. **The owner's override:** `enabled` false turns the lock off entirely, so settings never lock
   for movement. This departs from CLAUDE.md's rider-safety rule **by the owner's explicit
   choice** (2026-10-31). The app asks for confirmation with a warning before turning it off,
   and shows that it's off. The default stays on.

## Consequences

- CLAUDE.md's rule now reads "locked while moving, unless the rider turned the lock off in the
  app (ADR-0011)". Don't remove the switch or the warning without the owner.
- Other "while moving" rules (no ornament motion, ride-time text sizes) are unaffected: they
  follow the detected motion whatever the switch says.
- Thresholds can be set so low that walking locks the settings, which is handy for testing.
