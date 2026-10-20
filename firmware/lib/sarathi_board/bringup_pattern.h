// Phase 0 display check: colour fills, then a centred ring. Remove once the UI shell exists.

#pragma once

namespace sarathi::board {

class Display;

// Blocks for about 8 s: red, green, blue and white for 2 s each, each labelled with its
// expected name, then a white ring on black that is left on screen.
void RunBringUpPattern(Display& display);

}  // namespace sarathi::board
