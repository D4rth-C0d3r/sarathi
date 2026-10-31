package io.github.d4rthc0d3r.sarathi.core.ble

import io.github.d4rthc0d3r.sarathi.core.protocol.HelloDevice

/** The link to the dash, as the app shows it. */
sealed interface LinkState {
    /** Not started (no association, or stopped). */
    data object Stopped : LinkState

    /** Waiting for the dash to be in range and on; the Bluetooth stack connects when it is. */
    data object Waiting : LinkState

    /** Bonding: the phone and the dash both show a code to compare. */
    data object Pairing : LinkState

    /** Connected; setting up (MTU, service, notifications, hello). */
    data object Connecting : LinkState

    data class Ready(
        val dash: HelloDevice,
    ) : LinkState

    data class Incompatible(
        val dash: HelloDevice,
        val updateApp: Boolean,
    ) : LinkState

    /** Something failed; the link tries again after a pause. [problem] says what the rider can do. */
    data class Failed(
        val problem: LinkProblem,
        val detail: String,
    ) : LinkState
}

enum class LinkProblem {
    /** Nearby devices permission missing. */
    NO_PERMISSION,

    /** Bluetooth is off. */
    BLUETOOTH_OFF,

    /** The pairing was refused or timed out. */
    PAIRING_FAILED,

    /** The dash no longer knows this phone (Forget phones): forget it here too and pair again. */
    STALE_PAIRING,

    /** The link came up without what protocol v1 needs (MTU, service). */
    SETUP_FAILED,

    /** Anything else; the link retries. */
    OTHER,
}
