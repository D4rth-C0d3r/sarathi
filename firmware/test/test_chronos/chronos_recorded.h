// Chronos packets as the phone writes them, one array per BLE write.
//
// kRec*: recorded from the Chronos app on Android on 2026-10-24, with a throwaway recorder
// build that logged only these packet types (docs/phase3-findings.md). The caller's name is
// replaced by "Contact A", which has the same length, so the length bytes still match.
//
// Navigation hasn't been recorded yet: kSyn* packets are built byte for byte from the format
// in fbiego/chronos-esp32 1.9.1 (ChronosESP32.cpp, commands 0xEE and 0xEF).

#pragma once

#include <cstdint>

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

// Time 2026-10-24 19:26:30.
constexpr uint8_t kRecTime[] = {0xab, 0x00, 0x0b, 0xff, 0x93, 0x80, 0x00,
                                0x07, 0xea, 0x0a, 0x18, 0x13, 0x1a, 0x1e};

// Phone battery 77 %, not charging.
constexpr uint8_t kRecBattery[] = {0xab, 0x00, 0x05, 0xfe, 0x91, 0x80, 0x00, 0x4d};

// The 0xFF variant of the battery command; the library ignores it, and so do we.
constexpr uint8_t kRecBatteryFf[] = {0xab, 0x00, 0x04, 0xff, 0x91, 0x80, 0x01};

// Music state without a media session: paused, app "Music", empty package name.
constexpr uint8_t kRecMusicState[] = {0xab, 0x00, 0x11, 0xfe, 0x9d, 0x80, 0x00, 0x88, 0x88, 0x88,
                                      0xff, 0xff, 0xff, 0x4d, 0x75, 0x73, 0x69, 0x63, 0x00, 0x00};

// Music title "No active media": declares 22 bytes, but the phone sent only these 20 and never
// the rest (the next write started a new packet).
constexpr uint8_t kRecMusicTitleCut[] = {0xab, 0x00, 0x13, 0xfe, 0x9d, 0x81, 0x0f,
                                         0x4e, 0x6f, 0x20, 0x61, 0x63, 0x74, 0x69,
                                         0x76, 0x65, 0x20, 0x6d, 0x65, 0x64};

// Music artist "Chronos".
constexpr uint8_t kRecMusicArtist[] = {0xab, 0x00, 0x0b, 0xfe, 0x9d, 0x82, 0x07,
                                       0x43, 0x68, 0x72, 0x6f, 0x6e, 0x6f, 0x73};

// Incoming call from a saved contact (redacted).
constexpr uint8_t kRecRingerStart[] = {0xab, 0x00, 0x0e, 0xff, 0x72, 0x80, 0x01, 0x02, 0x43,
                                       0x6f, 0x6e, 0x74, 0x61, 0x63, 0x74, 0x20, 0x41};

// The phone stopped ringing (here: the call rang out).
constexpr uint8_t kRecRingerStop[] = {0xab, 0x00, 0x05, 0xff, 0x72, 0x80, 0x02, 0x02};

// NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
