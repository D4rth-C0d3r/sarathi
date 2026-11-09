package io.github.d4rthc0d3r.sarathi.core.ble

import io.github.d4rthc0d3r.sarathi.core.protocol.Framing
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/**
 * The sending half of a session (protocol.md sections 1 and 5): frames each message, waits for
 * the dash's flow credit before each frame, and writes them in order. One message at a time,
 * except bulk ones (map chunks), which let other messages through between their frames.
 */
internal class FrameWriter(
    private val port: GattPort,
    private val onFrameSent: () -> Unit,
) {
    private data class Credit(
        val window: Int,
        val consumed: Long,
        val closed: Boolean,
    )

    private val lock = Mutex()
    private val credit = MutableStateFlow(Credit(window = 0, consumed = 0, closed = false))
    private var sent = 0L
    private var msgId = 0

    // A bulk message in progress, and whether another multi-frame message cut it short.
    private var bulkActive = false
    private var bulkCut = false

    /** Hello goes before any flow credit, with a response (protocol.md section 1). */
    suspend fun hello(payload: ByteArray) =
        lock.withLock { frames(TYPE_HELLO, payload).forEach { write(it, hello = true) } }

    /** Waits for the dash's first credit after hello; false if the link closed. */
    suspend fun awaitFirstCredit(): Boolean = !credit.first { it.window > 0 || it.closed }.closed

    suspend fun send(
        type: Int,
        payload: ByteArray,
    ) = lock.withLock {
        val frames = frames(type, payload)
        // Starting another multi-frame message abandons an unfinished bulk one on the dash.
        if (frames.size > 1 && bulkActive) bulkCut = true
        frames.forEach { write(it, hello = false) }
    }

    /** Frame by frame, letting other messages in between; false if one cut it short. */
    suspend fun sendBulk(
        type: Int,
        payload: ByteArray,
    ): Boolean {
        val frames = lock.withLock { frames(type, payload) }
        bulkActive = true
        bulkCut = false
        try {
            for (frame in frames) {
                val cut = lock.withLock { bulkCut.also { if (!it) write(frame, hello = false) } }
                if (cut) return false
            }
            return true
        } finally {
            bulkActive = false
        }
    }

    fun onFlow(
        window: Int,
        consumed: Long,
    ) {
        credit.value = credit.value.copy(window = window, consumed = consumed)
    }

    fun close() {
        credit.value = credit.value.copy(closed = true)
    }

    private fun frames(
        type: Int,
        payload: ByteArray,
    ): List<ByteArray> {
        // Android refuses GATT writes over 512 bytes, whatever the MTU (517 gives 514-byte frames).
        val frames =
            Framing.fragment(
                type,
                msgId and BYTE_MASK,
                payload,
                minOf(
                    port.mtu,
                    MAX_WRITE_BYTES + ATT_HEADER_BYTES,
                ),
            )
        msgId++
        return frames
    }

    private suspend fun write(
        frame: ByteArray,
        hello: Boolean,
    ) {
        // Before the first flow, nothing but hello (protocol.md section 1).
        if (!hello) {
            val now = credit.first { it.closed || (sent - it.consumed).mod(U32) < it.window }
            if (now.closed) throw LinkClosedException()
        }
        port.write(frame, withResponse = hello)
        sent = (sent + 1).mod(U32)
        onFrameSent()
    }

    private companion object {
        const val TYPE_HELLO = 0x01
        const val BYTE_MASK = 0xFF
        const val U32 = 1L shl 32
    }
}

/** Android's longest GATT attribute value (GATT_MAX_ATTR_LEN): frames never exceed it. */
internal const val MAX_WRITE_BYTES = 512
private const val ATT_HEADER_BYTES = 3
