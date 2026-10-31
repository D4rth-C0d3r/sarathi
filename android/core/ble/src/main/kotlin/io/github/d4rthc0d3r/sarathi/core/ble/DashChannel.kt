package io.github.d4rthc0d3r.sarathi.core.ble

import io.github.d4rthc0d3r.sarathi.core.protocol.BinaryMessage
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
}
