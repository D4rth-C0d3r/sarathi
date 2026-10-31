package io.github.d4rthc0d3r.sarathi.core.protocol

/**
 * Binary messages (protocol.md section 4). Each checks the spec's limits when built, so
 * decoding is reading the fields and building.
 */
sealed interface BinaryMessage {
    val type: Int

    /** Writes the fields after the version byte. */
    fun write(w: ByteWriter)
}

object BinaryCodec {
    const val VERSION = 1

    private val readers: Map<Int, (ByteReader) -> BinaryMessage> =
        mapOf(
            MsgType.POSE to Pose::read,
            MsgType.ROUTE to Route::read,
            MsgType.TRAFFIC to Traffic::read,
            MsgType.CHUNK to Chunk::read,
            MsgType.CHUNK_HAVE to ChunkHave::read,
        )

    /** Throws [ProtocolException] with the drop reason. */
    fun decode(
        type: Int,
        payload: ByteArray,
    ): BinaryMessage {
        val read = readers[type] ?: drop("unknown_type")
        if (payload.isEmpty()) drop("invalid", "empty")
        if (payload[0].toInt() != VERSION) drop("ver_unknown")
        val r = ByteReader(payload)
        r.u8()
        return try {
            read(r).also { r.done() }
        } catch (e: IllegalArgumentException) {
            drop("invalid", e.message ?: "", e)
        }
    }

    fun encode(message: BinaryMessage): ByteArray {
        val w = ByteWriter()
        w.u8(VERSION)
        message.write(w)
        val bytes = w.toByteArray()
        require(bytes.size <= Framing.MAX_MESSAGE) { "${bytes.size} bytes, over ${Framing.MAX_MESSAGE}" }
        return bytes
    }
}

data class Point(
    val x: Int,
    val y: Int,
)

data class Pose(
    val epoch: Int,
    val tMs: Long,
    val xDm: Int,
    val yDm: Int,
    val headingCdeg: Int,
    val speedCms: Int,
    val accM: Int = ACC_UNKNOWN,
) : BinaryMessage {
    override val type get() = MsgType.POSE

    init {
        requireU16(epoch, "epoch")
        requireU32(tMs, "t_ms")
        require(headingCdeg in 0..MAX_HEADING) { "heading_cdeg" }
        requireU16(speedCms, "speed_cms")
        require(accM in 0..U8_MAX) { "acc_m" }
    }

    override fun write(w: ByteWriter) {
        w.u16(epoch)
        w.u32(tMs)
        w.i32(xDm)
        w.i32(yDm)
        w.u16(headingCdeg)
        w.u16(speedCms)
        w.u8(accM)
    }

    companion object {
        const val ACC_UNKNOWN = 255
        private const val MAX_HEADING = 35_999

        fun read(r: ByteReader) = Pose(r.u16(), r.u32(), r.i32(), r.i32(), r.u16(), r.u16(), r.u8())
    }
}

/** [maneuver] is a [Maneuver] code; unknown codes are drawn as straight. */
data class RouteMarker(
    val point: Int,
    val maneuver: Int,
    val exit: Int,
)

data class Route(
    val routeId: Long,
    val version: Int,
    val backend: Routing,
    val epoch: Int,
    val mode: Int,
    val quantumDm: Int,
    val points: List<Point>,
    val markers: List<RouteMarker>,
) : BinaryMessage {
    override val type get() = MsgType.ROUTE

    init {
        requireU32(routeId, "route_id")
        requireU16(version, "version")
        requireU16(epoch, "epoch")
        require(mode == MODE_TWO_WHEELER || mode == MODE_DRIVING) { "mode" }
        require(quantumDm in 1..U8_MAX) { "quantum_dm" }
        require(points.size in 2..MAX_POINTS) { "n_points" }
        require(points.all { it.x in COORDS && it.y in COORDS }) { "coordinates" }
        require(markers.size <= MAX_MARKERS) { "n_markers" }
        require(markers.zipWithNext().all { (a, b) -> a.point < b.point }) { "marker order" }
        require(markers.all { it.point in points.indices && it.maneuver in 0..U8_MAX && it.exit in 0..U8_MAX }) {
            "marker"
        }
    }

    override fun write(w: ByteWriter) {
        w.u32(routeId)
        w.u16(version)
        w.u8(backend.ordinal)
        w.u16(epoch)
        w.u8(mode)
        w.u8(quantumDm)
        w.varint(points.size.toLong())
        w.points(points)
        w.varint(markers.size.toLong())
        markers.forEach {
            w.varint(it.point.toLong())
            w.u8(it.maneuver)
            w.u8(it.exit)
        }
    }

    companion object {
        const val MODE_TWO_WHEELER = 0
        const val MODE_DRIVING = 1
        const val MAX_POINTS = 4000
        const val MAX_MARKERS = 256
        private const val COORD_LIMIT = 1 shl 30
        val COORDS = -COORD_LIMIT until COORD_LIMIT

        fun read(r: ByteReader): Route {
            val id = r.u32()
            val version = r.u16()
            val backend = r.u8()
            val epoch = r.u16()
            val mode = r.u8()
            val quantum = r.u8()
            require(backend < Routing.entries.size) { "backend" }
            val n = r.varint()
            require(n in 2..MAX_POINTS) { "n_points" }
            val points = r.points(n.toInt())
            val m = r.varint()
            require(m <= MAX_MARKERS) { "n_markers" }
            val markers =
                List(m.toInt()) { RouteMarker(r.varint().coerceAtMost(Int.MAX_VALUE.toLong()).toInt(), r.u8(), r.u8()) }
            return Route(id, version, Routing.entries[backend], epoch, mode, quantum, points, markers)
        }
    }
}

enum class TrafficLevel {
    FREE,
    SLOW,
    JAM,
    UNKNOWN,
}

data class TrafficEntry(
    val offsetM: Long,
    val lengthM: Long,
    val level: TrafficLevel,
)

data class Traffic(
    val routeId: Long,
    val version: Int,
    val entries: List<TrafficEntry>,
) : BinaryMessage {
    override val type get() = MsgType.TRAFFIC

    init {
        requireU32(routeId, "route_id")
        requireU16(version, "version")
        require(entries.size <= MAX_ENTRIES) { "n" }
        require(entries.all { it.offsetM in 0..U32_MAX && it.lengthM in 1..U32_MAX }) { "entry" }
        require(entries.zipWithNext().all { (a, b) -> b.offsetM >= a.offsetM + a.lengthM }) { "overlap" }
    }

    override fun write(w: ByteWriter) {
        w.u32(routeId)
        w.u16(version)
        w.varint(entries.size.toLong())
        entries.forEach {
            w.varint(it.offsetM)
            w.varint(it.lengthM)
            w.u8(it.level.ordinal)
        }
    }

    companion object {
        const val MAX_ENTRIES = 256

        fun read(r: ByteReader): Traffic {
            val id = r.u32()
            val version = r.u16()
            val n = r.varint()
            require(n <= MAX_ENTRIES) { "n" }
            val entries =
                List(n.toInt()) {
                    val offset = r.varint()
                    val length = r.varint()
                    val level = r.u8()
                    require(level < TrafficLevel.entries.size) { "level" }
                    TrafficEntry(offset, length, TrafficLevel.entries[level])
                }
            return Traffic(id, version, entries)
        }
    }
}

enum class Provenance(
    val code: Int,
) {
    OSM_OPENFREEMAP(1),
    OSM_PROTOMAPS_OFFLINE(2),
}

/** A road line (kind = class 0-6) or a polygon (kind 0 water, 1 park). */
data class Shape(
    val kind: Int,
    val points: List<Point>,
)

data class Chunk(
    val key: ChunkKey,
    val provenance: Provenance,
    val epoch: Int,
    val originXM: Int,
    val originYM: Int,
    val quantumDm: Int,
    val extent: Int,
    val lines: List<Shape>,
    val polys: List<Shape>,
) : BinaryMessage {
    override val type get() = MsgType.CHUNK

    init {
        requireU16(epoch, "epoch")
        require(quantumDm in 1..U8_MAX) { "quantum_dm" }
        require(extent in 1..U16_MAX) { "extent" }
        require(lines.size <= MAX_LINES && polys.size <= MAX_POLYS) { "shape count" }
        require(lines.all { it.kind in 0..MAX_CLASS && it.points.size in 2..MAX_SHAPE_POINTS }) { "line" }
        require(polys.all { it.kind in 0..1 && it.points.size in MIN_POLY_POINTS..MAX_SHAPE_POINTS }) { "polygon" }
        require((lines + polys).sumOf { it.points.size } <= MAX_POINTS) { "points" }
        val range = -OVERSCAN..extent + OVERSCAN
        require((lines + polys).all { s -> s.points.all { it.x in range && it.y in range } }) { "coordinates" }
    }

    override fun write(w: ByteWriter) {
        w.u8(key.level)
        w.u32(key.tileX)
        w.u32(key.tileY)
        w.u32(key.dataVersion)
        w.u8(provenance.code)
        w.u16(epoch)
        w.i32(originXM)
        w.i32(originYM)
        w.u8(quantumDm)
        w.u16(extent)
        for (shapes in listOf(lines, polys)) {
            w.varint(shapes.size.toLong())
            shapes.forEach {
                w.u8(it.kind)
                w.varint(it.points.size.toLong())
                w.points(it.points)
            }
        }
    }

    companion object {
        const val MAX_LINES = 4096
        const val MAX_POLYS = 1024
        const val MAX_SHAPE_POINTS = 1024
        const val MAX_POINTS = 8192
        const val OVERSCAN = 64
        private const val MAX_CLASS = 6
        private const val MIN_POLY_POINTS = 3

        fun read(r: ByteReader): Chunk {
            val level = r.u8()
            val key = ChunkKey(level, r.u32(), r.u32(), r.u32())
            val provenanceCode = r.u8()
            val provenance =
                requireNotNull(Provenance.entries.firstOrNull { it.code == provenanceCode }) { "provenance" }
            val epoch = r.u16()
            val ox = r.i32()
            val oy = r.i32()
            val quantum = r.u8()
            val extent = r.u16()
            val budget = intArrayOf(MAX_POINTS)
            val lines = readShapes(r, MAX_LINES, budget)
            val polys = readShapes(r, MAX_POLYS, budget)
            return Chunk(key, provenance, epoch, ox, oy, quantum, extent, lines, polys)
        }

        /** Checks counts before reading points, so a hostile count can't make it allocate. */
        private fun readShapes(
            r: ByteReader,
            max: Int,
            budget: IntArray,
        ): List<Shape> {
            val n = r.varint()
            require(n <= max) { "shape count" }
            return List(n.toInt()) {
                val kind = r.u8()
                val count = r.varint()
                require(count <= MAX_SHAPE_POINTS && count <= budget[0]) { "points" }
                budget[0] -= count.toInt()
                Shape(kind, r.points(count.toInt()))
            }
        }
    }
}

data class ChunkHave(
    val epoch: Int,
    val entries: List<ChunkKey>,
) : BinaryMessage {
    override val type get() = MsgType.CHUNK_HAVE

    init {
        requireU16(epoch, "epoch")
        require(entries.size <= MAX_ENTRIES) { "n" }
    }

    override fun write(w: ByteWriter) {
        w.u16(epoch)
        w.varint(entries.size.toLong())
        entries.forEach {
            w.u8(it.level)
            w.u32(it.tileX)
            w.u32(it.tileY)
            w.u32(it.dataVersion)
        }
    }

    companion object {
        const val MAX_ENTRIES = 512

        fun read(r: ByteReader): ChunkHave {
            val epoch = r.u16()
            val n = r.varint()
            require(n <= MAX_ENTRIES) { "n" }
            return ChunkHave(epoch, List(n.toInt()) { ChunkKey(r.u8(), r.u32(), r.u32(), r.u32()) })
        }
    }
}
