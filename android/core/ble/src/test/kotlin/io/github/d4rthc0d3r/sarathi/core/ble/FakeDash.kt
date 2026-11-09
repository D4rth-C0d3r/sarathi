package io.github.d4rthc0d3r.sarathi.core.ble

import io.github.d4rthc0d3r.sarathi.core.protocol.BinaryCodec
import io.github.d4rthc0d3r.sarathi.core.protocol.BinaryMessage
import io.github.d4rthc0d3r.sarathi.core.protocol.Flow
import io.github.d4rthc0d3r.sarathi.core.protocol.FrameEvent
import io.github.d4rthc0d3r.sarathi.core.protocol.Framing
import io.github.d4rthc0d3r.sarathi.core.protocol.HelloDevice
import io.github.d4rthc0d3r.sarathi.core.protocol.JsonCodec
import io.github.d4rthc0d3r.sarathi.core.protocol.JsonMessage
import io.github.d4rthc0d3r.sarathi.core.protocol.MsgType
import io.github.d4rthc0d3r.sarathi.core.protocol.Reassembler
import io.github.d4rthc0d3r.sarathi.core.protocol.Sender

/**
 * Plays the dash for session tests: reassembles and decodes every frame the phone writes (so
 * anything invalid fails the test), answers hello, and grants flow credit when told to.
 */
class FakeDash(
    override val mtu: Int = Framing.MIN_MTU,
    private val window: Int = 32,
    private val protoMajor: Int = 1,
) : GattPort {
    lateinit var session: PhoneLinkSession
    val received = mutableListOf<Any>()
    val writes = mutableListOf<Pair<Int, Boolean>>() // frame size, with response

    /** Messages the dash dropped (a bulk message cut short by another multi-frame one). */
    val drops = mutableListOf<String>()
    var consumed = 0L
        private set
    private val reassembler = Reassembler()
    private var msgId = 0

    override suspend fun write(
        frame: ByteArray,
        withResponse: Boolean,
    ) {
        writes += frame.size to withResponse
        consumed++
        for (event in reassembler.feed(frame)) {
            if (event is FrameEvent.Dropped) {
                // Only an abandoned message is allowed; anything else is the phone's bug.
                check(event.reason.wire == "cut_short") { "phone sent a bad frame: $event" }
                drops += event.reason.wire
                continue
            }
            check(event is FrameEvent.Message)
            val message =
                if (event.type >= MsgType.POSE) {
                    BinaryCodec.decode(event.type, event.payload)
                } else {
                    JsonCodec.decode(event.type, event.payload, Sender.PHONE)
                }
            received += message
            if (event.type == MsgType.HELLO) {
                reply(
                    HelloDevice(
                        "0.5.0",
                        500,
                        listOf("classic"),
                        0,
                        Framing.MAX_MESSAGE,
                        null,
                        null,
                        protoMajor = protoMajor,
                    ),
                )
                if (protoMajor == 1) grant()
            }
        }
    }

    /** Sends flow with everything consumed so far. */
    fun grant() = reply(Flow(window, consumed))

    fun reply(message: BinaryMessage) {
        for (frame in Framing.fragment(message.type, msgId++ and 0xFF, BinaryCodec.encode(message), mtu)) {
            session.onFrame(frame)
        }
    }

    fun reply(message: JsonMessage) {
        for (frame in Framing.fragment(message.type, msgId++ and 0xFF, JsonCodec.encode(message), mtu)) {
            session.onFrame(frame)
        }
    }
}
