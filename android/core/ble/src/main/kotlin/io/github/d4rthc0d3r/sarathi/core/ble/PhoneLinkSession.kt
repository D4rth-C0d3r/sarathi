package io.github.d4rthc0d3r.sarathi.core.ble

import io.github.d4rthc0d3r.sarathi.core.protocol.BinaryCodec
import io.github.d4rthc0d3r.sarathi.core.protocol.BinaryMessage
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

    private val _stats = MutableStateFlow(SessionStats())
    val stats: StateFlow<SessionStats> = _stats.asStateFlow()

    private val reassembler = Reassembler()
    private val sendLock = Mutex()
    private val credit = MutableStateFlow(Credit(window = 0, consumed = 0, closed = false))
    private var sent = 0L
    private var msgId = 0

    private data class Credit(
        val window: Int,
        val consumed: Long,
        val closed: Boolean,
    )

    /**
     * Sends hello and waits for the dash's answer and first flow credit. Returns the state it
     * ends in: [SessionState.Ready] or [SessionState.Incompatible]. Throws on timeout.
     */
    suspend fun start(timeoutMs: Long = HELLO_TIMEOUT_MS): SessionState {
        sendLock.withLock {
            sendFrames(MsgType.HELLO, JsonCodec.encode(HelloPhone(appVersion, caps)), firstIsHello = true)
        }
        return withTimeout(timeoutMs) {
            val answered = state.first { it !is SessionState.Starting }
            if (answered is SessionState.Ready) credit.first { it.window > 0 || it.closed }
            answered
        }
    }

    /** Sends a JSON message once the dash has said hello, within the flow window. */
    suspend fun send(message: JsonMessage) = sendPayload(message.type, JsonCodec.encode(message))

    suspend fun send(message: BinaryMessage) = sendPayload(message.type, BinaryCodec.encode(message))

    private suspend fun sendPayload(
        type: Int,
        payload: ByteArray,
    ) {
        if (state.value is SessionState.Closed) throw LinkClosedException()
        check(state.value is SessionState.Ready) { "the dash isn't ready" }
        sendLock.withLock { sendFrames(type, payload, firstIsHello = false) }
    }

    private suspend fun sendFrames(
        type: Int,
        payload: ByteArray,
        firstIsHello: Boolean,
    ) {
        val frames = Framing.fragment(type, msgId and BYTE_MASK, payload, port.mtu)
        msgId++
        for (frame in frames) {
            // Before the first flow, nothing but hello (protocol.md section 1).
            if (!firstIsHello) {
                val now = credit.first { it.closed || (sent - it.consumed).mod(U32) < it.window }
                if (now.closed) throw LinkClosedException()
            }
            port.write(frame, withResponse = firstIsHello)
            sent = (sent + 1).mod(U32)
            _stats.value = _stats.value.copy(framesSent = _stats.value.framesSent + 1)
        }
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
        if (event.type >= MsgType.POSE) {
            dropped("binary 0x%02x".format(event.type)) // chunk_have waits for the map step
            return
        }
        val message =
            try {
                JsonCodec.decode(event.type, event.payload, Sender.DEVICE)
            } catch (e: ProtocolException) {
                dropped(e.reason)
                return
            }
        _stats.value = _stats.value.copy(messagesReceived = _stats.value.messagesReceived + 1)
        when (message) {
            is HelloDevice -> onHello(message)
            is Flow -> credit.value = credit.value.copy(window = message.window, consumed = message.consumed)
            else -> if (state.value is SessionState.Ready) _received.tryEmit(message)
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
        credit.value = credit.value.copy(closed = true)
    }

    private companion object {
        const val HELLO_TIMEOUT_MS = 10_000L
        const val RECEIVED_BUFFER = 32
        const val BYTE_MASK = 0xFF
        const val U32 = 1L shl 32
    }
}
