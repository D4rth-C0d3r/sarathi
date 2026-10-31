package io.github.d4rthc0d3r.sarathi.core.protocol

private const val BYTE_MASK = 0xFF
private const val BITS_PER_BYTE = 8
private const val VARINT_PAYLOAD = 0x7F
private const val VARINT_MORE = 0x80
private const val VARINT_SHIFT = 7
private const val VARINT_MAX_BYTES = 5
private const val U32_BYTES = 4

/** Reads little-endian fields and varints; anything short or malformed is `invalid`. */
class ByteReader(
    private val data: ByteArray,
) {
    private var pos = 0

    fun u8(): Int {
        if (pos >= data.size) throw ProtocolException("invalid", "short")
        return data[pos++].toInt() and BYTE_MASK
    }

    fun u16(): Int = fixed(2).toInt()

    fun u32(): Long = fixed(U32_BYTES)

    fun i32(): Int = fixed(U32_BYTES).toInt()

    /** Unsigned LEB128, at most 5 bytes, below 2^32. */
    fun varint(): Long {
        var value = 0L
        repeat(VARINT_MAX_BYTES) { n ->
            val b = u8()
            value = value or ((b and VARINT_PAYLOAD).toLong() shl (VARINT_SHIFT * n))
            if (b and VARINT_MORE == 0) {
                if (value > U32_MAX) throw ProtocolException("invalid", "varint over 32 bits")
                return value
            }
        }
        throw ProtocolException("invalid", "varint over 5 bytes")
    }

    fun zigzag(): Long {
        val u = varint()
        return (u ushr 1) xor -(u and 1)
    }

    /** n points as zigzag deltas, the first from (0, 0). */
    fun points(n: Int): List<Point> {
        var x = 0L
        var y = 0L
        return List(n) {
            x += zigzag()
            y += zigzag()
            require(x in Int.MIN_VALUE..Int.MAX_VALUE && y in Int.MIN_VALUE..Int.MAX_VALUE) { "point" }
            Point(x.toInt(), y.toInt())
        }
    }

    fun done() {
        if (pos != data.size) throw ProtocolException("invalid", "${data.size - pos} bytes left over")
    }

    private fun fixed(n: Int): Long {
        var v = 0L
        for (i in 0 until n) v = v or (u8().toLong() shl (BITS_PER_BYTE * i))
        return v
    }
}

class ByteWriter {
    private val out = java.io.ByteArrayOutputStream()

    fun u8(v: Int) = out.write(v and BYTE_MASK)

    fun u16(v: Int) = fixed(v.toLong(), 2)

    fun u32(v: Long) = fixed(v, U32_BYTES)

    fun i32(v: Int) = fixed(v.toLong(), U32_BYTES)

    fun varint(value: Long) {
        require(value in 0..U32_MAX) { "varint range" }
        var v = value
        do {
            val b = (v and VARINT_PAYLOAD.toLong()).toInt()
            v = v ushr VARINT_SHIFT
            u8(if (v != 0L) b or VARINT_MORE else b)
        } while (v != 0L)
    }

    fun points(points: List<Point>) {
        var px = 0L
        var py = 0L
        for (p in points) {
            delta(p.x - px)
            delta(p.y - py)
            px = p.x.toLong()
            py = p.y.toLong()
        }
    }

    fun toByteArray(): ByteArray = out.toByteArray()

    private fun delta(d: Long) {
        require(d in Int.MIN_VALUE..Int.MAX_VALUE) { "delta over 32 bits" }
        val n = d.toInt()
        varint(((n shl 1) xor (n shr (Int.SIZE_BITS - 1))).toLong() and U32_MAX)
    }

    private fun fixed(
        v: Long,
        n: Int,
    ) {
        for (i in 0 until n) u8((v shr (BITS_PER_BYTE * i)).toInt())
    }
}
