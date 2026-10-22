// Rollback support (ADR-0004 §4, §5). The pinned core's bootloader rolls back to the previous
// image if a freshly updated one resets before confirming itself.
//
// This file also defines verifyRollbackLater(), which stops Arduino from confirming every new
// image at boot. It sits beside ReadBootState() on purpose: main.cpp calls ReadBootState(), so
// the linker always pulls this object in. An override alone in a library archive would be
// dropped silently, and Arduino's weak default would confirm every image.

#pragma once

#include "boot_health.h"

namespace sarathi::ota {

struct BootState {
  bool pending_verify = false;  // a new image waiting to confirm itself
  bool rolled_back = false;     // an earlier update was rejected and we're on the old image
};

[[nodiscard]] BootState ReadBootState();

// Marks the running image valid, cancelling rollback. Returns false if that failed.
bool ConfirmBoot();

}  // namespace sarathi::ota
