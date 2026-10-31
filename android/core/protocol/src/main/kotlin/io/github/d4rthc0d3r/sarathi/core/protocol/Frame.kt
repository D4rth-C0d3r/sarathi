package io.github.d4rthc0d3r.sarathi.core.protocol

/**
 * Framing (docs/protocol/protocol.md section 1, ADR-0007): CRC-16, splitting a message into
 * frames for an ATT MTU, and reassembling frames with every length, index and identity checked.
 */
object Framing {
    const val MAX_MESSAGE = 15_360
    const val MAX_FRAGMENTS = 64
    const val FIRST_HEADER = 7
    const val NEXT_HEADER = 3
    const val ATT_HEADER = 3
    const val FIRST = 0x80
    const val LAST = 0x40
    const val INDEX_MASK = 0x3F
    const val MIN_MTU = 247
    const val TOTAL_OFFSET = 3
    const val CRC_OFFSET = 5

    private const val CRC_INITIAL = 0xFFFF
    private const val CRC_POLY = 0x1021
    private const val CRC_TOP_BIT = 0x8000
    private const val BITS_PER_BYTE = 8
    private const val BYTE_MASK = 0xFF
    private const val U16_MASK = 0xFFFF

    /** CRC-16/CCITT-FALSE: polynomial 0x1021, initial 0xFFFF, no reflection, no final XOR. */
    fun crc16(
        data: ByteArray,
        from: Int = 0,
        to: Int = data.size,
    ): Int {
        var crc = CRC_INITIAL
        for (i in from until to) {
            crc = crc xor ((data[i].toInt() and BYTE_MASK) shl BITS_PER_BYTE)
            repeat(BITS_PER_BYTE) {
                crc = if (crc and CRC_TOP_BIT != 0) (crc shl 1) xor CRC_POLY else crc shl 1
                crc = crc and U16_MASK
            }
        }
        return crc
    }

    /**
     * The frames of one message, in order. Each is at most `mtu - 3` bytes (one ATT write or
     * notification). Throws if the message breaks the limits at this MTU.
     */
    fun fragment(
        type: Int,
        msgId: Int,
        payload: ByteArray,
        mtu: Int,
    ): List<ByteArray> {
        require(payload.size <= MAX_MESSAGE) { "message longer than $MAX_MESSAGE bytes" }
        require(mtu >= ATT_HEADER + FIRST_HEADER + 1) { "MTU $mtu too small" }
        val firstCap = mtu - ATT_HEADER - FIRST_HEADER
        val nextCap = mtu - ATT_HEADER - NEXT_HEADER
        val count = 1 + if (payload.size > firstCap) (payload.size - firstCap + nextCap - 1) / nextCap else 0
        require(count <= MAX_FRAGMENTS) { "needs $count fragments at MTU $mtu" }
        val crc = crc16(payload)
        var sent = 0
        return List(count) { index ->
            val first = index == 0
            val header = if (first) FIRST_HEADER else NEXT_HEADER
            val take = minOf(payload.size - sent, if (first) firstCap else nextCap)
            val frame = ByteArray(header + take)
            frame[0] = type.toByte()
            frame[1] = msgId.toByte()
            frame[2] = (index or (if (first) FIRST else 0) or (if (index == count - 1) LAST else 0)).toByte()
            if (first) {
                putU16(frame, TOTAL_OFFSET, payload.size)
                putU16(frame, CRC_OFFSET, crc)
            }
            payload.copyInto(frame, header, sent, sent + take)
            sent += take
            frame
        }
    }

    internal fun putU16(
        out: ByteArray,
        at: Int,
        value: Int,
    ) {
        out[at] = (value and BYTE_MASK).toByte()
        out[at + 1] = (value shr BITS_PER_BYTE and BYTE_MASK).toByte()
    }

    internal fun u16(
        data: ByteArray,
        at: Int,
    ): Int = (data[at].toInt() and BYTE_MASK) or ((data[at + 1].toInt() and BYTE_MASK) shl BITS_PER_BYTE)
}

/** Why a frame or message was dropped (protocol.md section 1); `wire` is the vectors' name. */
enum class Drop(
    val wire: String,
) {
    SHORT_FRAME("short_frame"),
    OUT_OF_ORDER("out_of_order"),
    TOO_LONG("too_long"),
    CUT_SHORT("cut_short"),
    STRAY("stray"),
    MISMATCH("mismatch"),
    LENGTH("length"),
    CRC("crc"),
}

sealed interface FrameEvent {
    class Message(
        val type: Int,
        val msgId: Int,
        val payload: ByteArray,
    ) : FrameEvent

    data class Dropped(
        val reason: Drop,
    ) : FrameEvent
}

/**
 * One multi-frame message at a time; single-frame messages may arrive between its fragments
 * and don't disturb it. A new multi-frame message abandons an unfinished one (`cut_short`).
 */
class Reassembler {
    private class Partial(
        val type: Int,
        val msgId: Int,
        val total: Int,
        val crc: Int,
    ) {
        val buffer = java.io.ByteArrayOutputStream(total)
        var nextIndex = 1
    }

    private var active: Partial? = null

    fun feed(frame: ByteArray): List<FrameEvent> {
        if (frame.size < Framing.NEXT_HEADER) return drop(Drop.SHORT_FRAME)
        val flags = frame[2].toInt() and BYTE_MASK
        return if (flags and Framing.FIRST != 0) first(frame, flags) else continuation(frame, flags)
    }

    private fun first(
        frame: ByteArray,
        flags: Int,
    ): List<FrameEvent> {
        val problem =
            when {
                frame.size < Framing.FIRST_HEADER -> Drop.SHORT_FRAME
                flags and Framing.INDEX_MASK != 0 -> Drop.OUT_OF_ORDER
                Framing.u16(frame, Framing.TOTAL_OFFSET) > Framing.MAX_MESSAGE -> Drop.TOO_LONG
                else -> null
            }
        return if (problem != null) drop(problem) else start(frame, flags)
    }

    /** A valid first frame: a whole message, or the start of one (abandoning any unfinished). */
    private fun start(
        frame: ByteArray,
        flags: Int,
    ): List<FrameEvent> {
        val type = frame[0].toInt() and BYTE_MASK
        val msgId = frame[1].toInt() and BYTE_MASK
        val total = Framing.u16(frame, Framing.TOTAL_OFFSET)
        val crc = Framing.u16(frame, Framing.CRC_OFFSET)
        val body = frame.copyOfRange(Framing.FIRST_HEADER, frame.size)
        if (flags and Framing.LAST != 0) return complete(type, msgId, total, crc, body)
        val events = if (active != null) listOf(FrameEvent.Dropped(Drop.CUT_SHORT)) else emptyList()
        active = if (body.size > total) null else Partial(type, msgId, total, crc).also { it.buffer.write(body) }
        return if (active == null) events + FrameEvent.Dropped(Drop.TOO_LONG) else events
    }

    private fun continuation(
        frame: ByteArray,
        flags: Int,
    ): List<FrameEvent> {
        val partial = active ?: return drop(Drop.STRAY)
        val body = frame.size - Framing.NEXT_HEADER
        val problem =
            when {
                (frame[0].toInt() and BYTE_MASK) != partial.type ||
                    (frame[1].toInt() and BYTE_MASK) != partial.msgId -> Drop.MISMATCH

                flags and Framing.INDEX_MASK != partial.nextIndex -> Drop.OUT_OF_ORDER

                partial.buffer.size() + body > partial.total -> Drop.TOO_LONG

                else -> null
            }
        if (problem != null) active = null
        return if (problem != null) drop(problem) else append(partial, frame, flags)
    }

    private fun append(
        partial: Partial,
        frame: ByteArray,
        flags: Int,
    ): List<FrameEvent> {
        partial.buffer.write(frame, Framing.NEXT_HEADER, frame.size - Framing.NEXT_HEADER)
        partial.nextIndex++
        if (flags and Framing.LAST == 0) return emptyList()
        active = null
        return complete(partial.type, partial.msgId, partial.total, partial.crc, partial.buffer.toByteArray())
    }

    private fun complete(
        type: Int,
        msgId: Int,
        total: Int,
        crc: Int,
        payload: ByteArray,
    ): List<FrameEvent> =
        when {
            payload.size != total -> drop(Drop.LENGTH)
            Framing.crc16(payload) != crc -> drop(Drop.CRC)
            else -> listOf(FrameEvent.Message(type, msgId, payload))
        }

    private fun drop(reason: Drop): List<FrameEvent> = listOf(FrameEvent.Dropped(reason))

    private companion object {
        const val BYTE_MASK = 0xFF
    }
}
