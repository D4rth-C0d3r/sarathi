#include "knob_decoder.h"

#include <array>

namespace sarathi::core {
namespace {

// Index: (previous AB << 2) | current AB. Invalid (skipped) transitions count as 0.
constexpr std::array<int8_t, 16> kTransitions = {
    0,  -1, 1,  0,   //
    1,  0,  0,  -1,  //
    -1, 0,  0,  1,   //
    0,  1,  -1, 0,   //
};

uint8_t PinState(bool a, bool b) { return static_cast<uint8_t>((a ? 2U : 0U) | (b ? 1U : 0U)); }

}  // namespace

KnobDecoder::KnobDecoder(uint8_t transitions_per_detent)
    : transitions_per_detent_(transitions_per_detent) {}

void KnobDecoder::Reset(bool a, bool b) {
  state_ = PinState(a, b);
  accumulated_ = 0;
}

int8_t KnobDecoder::Update(bool a, bool b) {
  const uint8_t next = PinState(a, b);
  const unsigned index = (static_cast<unsigned>(state_) << 2U) | next;  // always 0..15
  accumulated_ = static_cast<int8_t>(
      accumulated_ +
      kTransitions[index]);  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
  state_ = next;

  const auto detent = static_cast<int8_t>(transitions_per_detent_);
  if (accumulated_ >= detent) {
    accumulated_ = 0;
    return 1;
  }
  if (accumulated_ <= -detent) {
    accumulated_ = 0;
    return -1;
  }
  return 0;
}

}  // namespace sarathi::core
