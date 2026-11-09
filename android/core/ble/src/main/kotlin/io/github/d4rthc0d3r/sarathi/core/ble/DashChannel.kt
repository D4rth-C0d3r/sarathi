package io.github.d4rthc0d3r.sarathi.core.ble

import io.github.d4rthc0d3r.sarathi.core.protocol.BinaryMessage
import io.github.d4rthc0d3r.sarathi.core.protocol.ChunkHave
import io.github.d4rthc0d3r.sarathi.core.protocol.HelloDevice
import io.github.d4rthc0d3r.sarathi.core.protocol.JsonMessage
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * What a feature (calls, media, navigation) needs from the link to the dash, and nothing about
 * Bluetooth. [DashLink] is the real one; tests use a fake.
 */
interface DashChannel {
    /** True while the dash has said hello and messages may flow. */
    val ready: StateFlow<Boolean>

    /** Messages from the dash (call_action, media_cmd, style, ...). */
    val received: SharedFlow<JsonMessage>

    /** Sends when ready; throws [LinkClosedException] or IllegalStateException otherwise. */
    suspend fun send(message: JsonMessage)

    suspend fun send(message: BinaryMessage)

    /** The dash's hello while ready (its protocol version, map cache size), else null. */
    val dashHello: StateFlow<HelloDevice?>

    /** What the dash's map cache holds, after each hello (ADR-0009). */
    val chunkHave: SharedFlow<ChunkHave>

    /**
     * A large, low-priority message (a map chunk), sent frame by frame with other messages
     * between its frames. False if a multi-frame message cut it short: send it again later.
     */
    suspend fun sendBulk(message: BinaryMessage): Boolean
}
