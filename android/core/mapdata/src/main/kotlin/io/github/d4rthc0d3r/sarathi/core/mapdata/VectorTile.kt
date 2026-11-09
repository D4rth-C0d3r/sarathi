// Protocol-buffer and vector-tile field numbers, wire types and byte widths are the formats' own
// numbers; naming each one would only repeat the specifications.
@file:Suppress("MagicNumber")

package io.github.d4rthc0d3r.sarathi.core.mapdata

/** A point in tile units: 0..4096 across the tile, y down (Mapbox Vector Tile). */
data class TilePoint(
    val x: Double,
    val y: Double,
)

/** One feature of a layer: geometry type (1 point, 2 line, 3 polygon), tags and its rings. */
class TileFeature(
    val type: Int,
    val tags: Map<String, Any?>,
    val rings: List<List<TilePoint>>,
)

/**
 * The few parts of a Mapbox Vector Tile (protocol buffers) that map chunks use: the layers named
 * in [layers], features with tags and geometry, scaled to 4096 units whatever the layer's
 * extent. Our own small reader, so no protobuf library (ADR-0009).
 */
object VectorTile {
    /** The layers chunks read (OpenMapTiles schema). */
    val LAYERS = setOf("transportation", "water", "park")

    private const val EXTENT = 4096.0
    private const val DEFAULT_EXTENT = 4096
    private const val TILE_LAYERS = 3
    private const val LAYER_NAME = 1
    private const val LAYER_FEATURES = 2
    private const val LAYER_KEYS = 3
    private const val LAYER_VALUES = 4
    private const val LAYER_EXTENT = 5
    private const val FEATURE_TAGS = 2
    private const val FEATURE_TYPE = 3
    private const val FEATURE_GEOMETRY = 4
    private const val MOVE_TO = 1
    private const val CLOSE_PATH = 7

    fun read(
        data: ByteArray,
        layers: Set<String> = LAYERS,
    ): Map<String, List<TileFeature>> {
        val out = linkedMapOf<String, List<TileFeature>>()
        Protobuf(data).forEach { field ->
            if (field.number != TILE_LAYERS) return@forEach
            val layer = field.bytes()
            var name = ""
            val features = mutableListOf<ByteArray>()
            val keys = mutableListOf<String>()
            val values = mutableListOf<Any?>()
            var extent = DEFAULT_EXTENT
            Protobuf(layer).forEach { f ->
                when (f.number) {
                    LAYER_NAME -> name = f.string()
                    LAYER_FEATURES -> features += f.bytes()
                    LAYER_KEYS -> keys += f.string()
                    LAYER_VALUES -> values += value(f.bytes())
                    LAYER_EXTENT -> extent = f.varint.toInt()
                }
            }
            if (name !in layers) return@forEach
            val scale = EXTENT / extent
            out[name] = features.map { feature(it, keys, values, scale) }
        }
        return out
    }

    /** The same tile with only [layers], each copied byte for byte (fixtures stay small). */
    fun filter(
        data: ByteArray,
        layers: Set<String> = LAYERS,
    ): ByteArray {
        val out = java.io.ByteArrayOutputStream()
        Protobuf(data).forEach { field ->
            if (field.number != TILE_LAYERS) return@forEach
            var name = ""
            Protobuf(field.bytes()).forEach { if (it.number == LAYER_NAME) name = it.string() }
            if (name in layers) out.write(data, field.start, field.end - field.start)
        }
        return out.toByteArray()
    }

    private fun feature(
        data: ByteArray,
        keys: List<String>,
        values: List<Any?>,
        scale: Double,
    ): TileFeature {
        var tags = LongArray(0)
        var type = 0
        var geometry = LongArray(0)
        Protobuf(data).forEach { f ->
            when (f.number) {
                FEATURE_TAGS -> tags = f.packed()
                FEATURE_TYPE -> type = f.varint.toInt()
                FEATURE_GEOMETRY -> geometry = f.packed()
            }
        }
        val props = linkedMapOf<String, Any?>()
        for (k in 0 until tags.size - 1 step 2) props[keys[tags[k].toInt()]] = values[tags[k + 1].toInt()]
        return TileFeature(type, props, rings(geometry, scale))
    }

    private fun rings(
        cmds: LongArray,
        scale: Double,
    ): List<List<TilePoint>> {
        val rings = mutableListOf<MutableList<TilePoint>>()
        var x = 0L
        var y = 0L
        var i = 0
        while (i < cmds.size) {
            val cmd = (cmds[i] and 7).toInt()
            val count = (cmds[i] ushr 3).toInt()
            i++
            if (cmd == CLOSE_PATH) continue
            repeat(count) {
                x += unzigzag(cmds[i])
                y += unzigzag(cmds[i + 1])
                i += 2
                if (cmd == MOVE_TO) rings += mutableListOf<TilePoint>()
                rings.last() += TilePoint(x * scale, y * scale)
            }
        }
        return rings
    }

    private fun value(data: ByteArray): Any? {
        var result: Any? = null
        Protobuf(data).forEach { f ->
            if (result != null) return@forEach
            result =
                when (f.number) {
                    1 -> {
                        f.string()
                    }

                    2 -> {
                        java.lang.Float
                            .intBitsToFloat(f.fixed32())
                            .toDouble()
                    }

                    3 -> {
                        java.lang.Double.longBitsToDouble(f.fixed64())
                    }

                    4, 5 -> {
                        f.varint
                    }

                    6 -> {
                        unzigzag(f.varint)
                    }

                    7 -> {
                        f.varint != 0L
                    }

                    else -> {
                        null
                    }
                }
        }
        return result
    }

    private fun unzigzag(v: Long): Long = (v ushr 1) xor -(v and 1)
}

/** A protocol-buffer message read field by field. */
internal class Protobuf(
    private val data: ByteArray,
) {
    inner class Field(
        val number: Int,
        val start: Int,
        val end: Int,
        val varint: Long,
        private val from: Int,
        private val until: Int,
    ) {
        fun bytes(): ByteArray = data.copyOfRange(from, until)

        fun string(): String = String(data, from, until - from, Charsets.UTF_8)

        fun fixed32(): Int = (0 until 4).fold(0) { acc, k -> acc or ((data[from + k].toInt() and BYTE) shl (8 * k)) }

        fun fixed64(): Long = (0 until 8).fold(0L) { acc, k -> acc or ((data[from + k].toLong() and 0xFF) shl (8 * k)) }

        fun packed(): LongArray {
            val out = ArrayList<Long>()
            var i = from
            while (i < until) {
                val (v, next) = varintAt(data, i)
                out += v
                i = next
            }
            return out.toLongArray()
        }
    }

    fun forEach(block: (Field) -> Unit) {
        var i = 0
        while (i < data.size) {
            val start = i
            val (key, afterKey) = varintAt(data, i)
            i = afterKey
            val number = (key ushr 3).toInt()
            val wire = (key and 7).toInt()
            when (wire) {
                0 -> {
                    val (v, next) = varintAt(data, i)
                    block(Field(number, start, next, v, i, next))
                    i = next
                }

                1 -> {
                    block(Field(number, start, i + 8, 0, i, i + 8))
                    i += 8
                }

                2 -> {
                    val (len, next) = varintAt(data, i)
                    val end = next + len.toInt()
                    block(Field(number, start, end, 0, next, end))
                    i = end
                }

                5 -> {
                    block(Field(number, start, i + 4, 0, i, i + 4))
                    i += 4
                }

                else -> {
                    throw IllegalArgumentException("wire type $wire")
                }
            }
        }
    }

    private companion object {
        const val BYTE = 0xFF
        const val SEVEN_BITS = 0x7F
        const val MORE = 0x80
        const val MAX_SHIFT = 63

        fun varintAt(
            data: ByteArray,
            start: Int,
        ): Pair<Long, Int> {
            var i = start
            var shift = 0
            var result = 0L
            while (true) {
                require(i < data.size && shift <= MAX_SHIFT) { "truncated varint" }
                val b = data[i++].toInt() and BYTE
                result = result or ((b and SEVEN_BITS).toLong() shl shift)
                if (b and MORE == 0) return result to i
                shift += 7
            }
        }
    }
}
