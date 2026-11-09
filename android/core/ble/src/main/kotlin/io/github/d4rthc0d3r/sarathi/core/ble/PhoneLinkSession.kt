package io.github.d4rthc0d3r.sarathi.core.ble

import io.github.d4rthc0d3r.sarathi.core.protocol.BinaryCodec
import io.github.d4rthc0d3r.sarathi.core.protocol.BinaryMessage
import io.github.d4rthc0d3r.sarathi.core.protocol.ChunkHave
import io.github.d4rthc0d3r.sarathi.core.protocol.Flow
import io.github.d4rthc0d3r.sarathi.core.protocol.FrameEvent
import io.github.d4rthc0d3r.sarathi.core.protocol.Framing
import io.github.d4rthc0d3r.sarathi.core.protocol.HelloDevice
import io.github.d4rthc0d3r.sarathi.core.protocol.HelloPhone
import io.github.d4rthc0d3r.sarathi.core.protocol.JsonCodec
import io.github.d4rthc0d3r.sarathi.core.protocol.JsonMessage
import io.github.d4rthc0d3r.sarathi.core.protocol.MsgType
import io.github.d4rthc0d3r.sarathi.core.protocol.PhoneCap
import io.github.d4rthc0d3r.sarathi.core.protocol.ProtocolException
import io.github.d4rthc0d3r.sarathi.core.protocol.ProtocolVersion
import io.github.d4rthc0d3r.sarathi.core.protocol.Reassembler
import io.github.d4rthc0d3r.sarathi.core.protocol.Sender
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout

/** Where the protocol session is (protocol.md section 5, "Connect"). */
sealed interface SessionState {
    /** Hello sent, waiting for the dash's hello and first flow credit. */
    data object Starting : SessionState

    /** The dash answered; messages may flow. */
    data class Ready(
        val dash: HelloDevice,
    ) : SessionState

    /** The dash speaks another protocol major version (protocol.md section 6). */
    data class Incompatible(
        val dash: HelloDevice,
        val updateApp: Boolean,
    ) : SessionState

    data object Closed : SessionState
}

/** Thrown to a sender when the link closes while it waits. */
class LinkClosedException : Exception("the link to the dash is closed")

/** Counters for logs and the status screen. */
data class SessionStats(
    val framesSent: Long = 0,
    val messagesReceived: Long = 0,
    val dropped: Long = 0,
    val lastDrop: String = "",
)

/**
 * The phone's side of one connection to the dash (protocol v1, docs/protocol/protocol.md): hello,
 * flow-control credits, framing, and decoding what the dash sends. One per connection; the
 * transport creates a new one after every reconnect. Pure Kotlin: tested on the JVM with a fake
 * dash.
 */
class PhoneLinkSession(
    private val port: GattPort,
    private val appVersion: String,
    private val caps: Set<PhoneCap> = emptySet(),
) {
    private val _state = MutableStateFlow<SessionState>(SessionState.Starting)
    val state: StateFlow<SessionState> = _state.asStateFlow()

    private val _received =
        MutableSharedFlow<JsonMessage>(
            extraBufferCapacity = RECEIVED_BUFFER,
            onBufferOverflow = BufferOverflow.DROP_OLDEST,
        )

    /** Messages from the dash after its hello (call_action, media_cmd, style, ...). */
    val received: SharedFlow<JsonMessage> = _received.asSharedFlow()

    private val _chunkHave =
        MutableSharedFlow<ChunkHave>(extraBufferCapacity = 2, onBufferOverflow = BufferOverflow.DROP_OLDEST)

    /** What the dash's map cache holds, sent after its hello (ADR-0009). */
    val chunkHave: SharedFlow<ChunkHave> = _chunkHave.asSharedFlow()

    private val _stats = MutableStateFlow(SessionStats())
    val stats: StateFlow<SessionStats> = _stats.asStateFlow()

    private val reassembler = Reassembler()
    private val writer =
        FrameWriter(port) { _stats.value = _stats.value.copy(framesSent = _stats.value.framesSent + 1) }

    /**
     * Sends hello and waits for the dash's answer and first flow credit. Returns the state it
     * ends in: [SessionState.Ready] or [SessionState.Incompatible]. Throws on timeout.
     */
    suspend fun start(timeoutMs: Long = HELLO_TIMEOUT_MS): SessionState {
        writer.hello(JsonCodec.encode(HelloPhone(appVersion, caps)))
        return withTimeout(timeoutMs) {
            val answered = state.first { it !is SessionState.Starting }
            if (answered is SessionState.Ready) writer.awaitFirstCredit()
            answered
        }
    }

    /** Sends a JSON message once the dash has said hello, within the flow window. */
    suspend fun send(message: JsonMessage) {
        checkReady()
        writer.send(message.type, JsonCodec.encode(message))
    }

    suspend fun send(message: BinaryMessage) {
        checkReady()
        writer.send(message.type, BinaryCodec.encode(message))
    }

    /**
     * Sends a large, low-priority message (a map chunk) one frame at a time, letting other
     * messages through between its frames (protocol.md section 5). Returns false if a multi-frame
     * message cut it short (the dash then drops it): send it again later.
     */
    suspend fun sendBulk(message: BinaryMessage): Boolean {
        checkReady()
        return writer.sendBulk(message.type, BinaryCodec.encode(message))
    }

    private fun checkReady() {
        if (state.value is SessionState.Closed) throw LinkClosedException()
        check(state.value is SessionState.Ready) { "the dash isn't ready" }
    }

    /** One notification from the dash's `tx` characteristic. Never throws. */
    fun onFrame(frame: ByteArray) {
        for (event in reassembler.feed(frame)) {
            when (event) {
                is FrameEvent.Dropped -> dropped(event.reason.wire)
                is FrameEvent.Message -> onMessage(event)
            }
        }
    }

    private fun onMessage(event: FrameEvent.Message) {
        val message =
            try {
                if (event.type >= MsgType.POSE) {
                    BinaryCodec.decode(event.type, event.payload)
                } else {
                    JsonCodec.decode(event.type, event.payload, Sender.DEVICE)
                }
            } catch (e: ProtocolException) {
                dropped(e.reason)
                return
            }
        _stats.value = _stats.value.copy(messagesReceived = _stats.value.messagesReceived + 1)
        val ready = state.value is SessionState.Ready
        when (message) {
            is HelloDevice -> onHello(message)
            is Flow -> writer.onFlow(message.window, message.consumed)
            is ChunkHave -> if (ready) _chunkHave.tryEmit(message)
            is JsonMessage -> if (ready) _received.tryEmit(message)
            else -> dropped("binary 0x%02x".format(event.type)) // not one the dash sends
        }
    }

    private fun onHello(hello: HelloDevice) {
        _state.value =
            when {
                hello.protoMajor == ProtocolVersion.MAJOR -> SessionState.Ready(hello)
                else -> SessionState.Incompatible(hello, updateApp = hello.protoMajor > ProtocolVersion.MAJOR)
            }
    }

    private fun dropped(reason: String) {
        _stats.value = _stats.value.copy(dropped = _stats.value.dropped + 1, lastDrop = reason)
    }

    /** The link is gone: anything waiting to send fails, and nothing more is accepted. */
    fun close() {
        _state.value = SessionState.Closed
        writer.close()
    }

    private companion object {
        const val HELLO_TIMEOUT_MS = 10_000L
        const val RECEIVED_BUFFER = 32
    }
}
