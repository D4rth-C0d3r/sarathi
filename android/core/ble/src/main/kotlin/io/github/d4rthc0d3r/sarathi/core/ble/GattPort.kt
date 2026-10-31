package io.github.d4rthc0d3r.sarathi.core.ble

/**
 * What the protocol session needs from the Bluetooth transport: write one frame to the dash's
 * `rx` characteristic. The transport hands notifications from `tx` to [PhoneLinkSession.onFrame].
 */
interface GattPort {
    /** The ATT MTU in use; frames are at most `mtu - 3` bytes. */
    val mtu: Int

    /**
     * Writes one frame and suspends until the stack has taken it. [withResponse] is for the
     * phone's hello (protocol.md section 1); every other frame is written without response.
     * Throws if the link is gone.
     */
    suspend fun write(
        frame: ByteArray,
        withResponse: Boolean,
    )
}
