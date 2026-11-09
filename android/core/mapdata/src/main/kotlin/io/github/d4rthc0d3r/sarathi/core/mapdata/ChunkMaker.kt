package io.github.d4rthc0d3r.sarathi.core.mapdata

import io.github.d4rthc0d3r.sarathi.core.mapdata.TileGeometry.EXTENT
import io.github.d4rthc0d3r.sarathi.core.protocol.Chunk
import io.github.d4rthc0d3r.sarathi.core.protocol.ChunkKey
import io.github.d4rthc0d3r.sarathi.core.protocol.Point
import io.github.d4rthc0d3r.sarathi.core.protocol.Provenance
import io.github.d4rthc0d3r.sarathi.core.protocol.RideFrame
import io.github.d4rthc0d3r.sarathi.core.protocol.Shape
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.atan
import kotlin.math.ceil
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.sinh

/** A level of detail (ADR-0009 "Chunks"). */
enum class MapLevel(
    val code: Int,
    val zoom: Int,
    /** Road classes up to this one (0 motorway ... 6 service). */
    val maxClass: Int,
    val quantumDm: Int,
    val parks: Boolean,
    /** Water smaller than this isn't kept. */
    val minWaterM2: Double,
) {
    /** z14, every class to service, water and parks, 2 m. */
    L0(code = 0, zoom = 14, maxClass = 6, quantumDm = 20, parks = true, minWaterM2 = 0.0),

    /** z12, motorway to secondary, only large water, 8 m. */
    L1(code = 1, zoom = 12, maxClass = 3, quantumDm = 80, parks = false, minWaterM2 = 40_000.0),
    ;

    val quantumM: Double get() = quantumDm / DM_PER_M

    companion object {
        private const val DM_PER_M = 10.0

        fun forZoom(zoom: Int): MapLevel? = entries.firstOrNull { it.zoom == zoom }
    }
}

/** A road (class 0-6) or a polygon ring (kind 0 water, 1 park), in tile units. */
class TileShape(
    val kind: Int,
    val points: List<TilePoint>,
)

/**
 * One tile, processed and ready for any ride: what the phone caches (ADR-0009 "Phone cache").
 * Coordinates stay in tile units; [ChunkMaker.chunk] projects them into a ride frame.
 */
class TileShapes(
    val level: MapLevel,
    val tileX: Int,
    val tileY: Int,
    val lines: List<TileShape>,
    val polys: List<TileShape>,
)

/**
 * Vector tiles to the device's map chunks, as tools/make_chunks.py (the reference) does it.
 * [shapes] does the ride-independent work (classes, clip, merge, simplify); [chunk] projects
 * into a ride frame and quantizes.
 */
object ChunkMaker {
    val ROAD_CLASSES = listOf("motorway", "trunk", "primary", "secondary", "tertiary", "minor", "service")
    private const val MAX_LINE_POINTS = 1024
    private const val MIN_POLY_QUANTA2 = 16
    private const val A = 6_378_137.0 // WGS84 semi-major axis, as RideFrame
    private const val DEG = PI / 180
    private const val FULL_TURN = 360.0
    private const val HALF_TURN = 180.0
    private const val LINE = 2
    private const val POLYGON = 3
    private const val WATER = 0
    private const val PARK = 1
    private const val MIN_RING = 3

    fun shapes(
        tile: Map<String, List<TileFeature>>,
        level: MapLevel,
        tileX: Int,
        tileY: Int,
    ): TileShapes {
        val tol = level.quantumM / metresPerUnit(level.zoom, tileY)
        return TileShapes(level, tileX, tileY, roads(tile, level, tol), areas(tile, level, tileY, tol))
    }

    private fun roads(
        tile: Map<String, List<TileFeature>>,
        level: MapLevel,
        tol: Double,
    ): List<TileShape> {
        val byClass = sortedMapOf<Int, MutableList<List<TilePoint>>>()
        for (f in tile["transportation"].orEmpty()) {
            val c = ROAD_CLASSES.indexOf(f.tags["class"] as? String)
            if (f.type == LINE && c >= 0 && c <= level.maxClass) {
                for (ring in f.rings) byClass.getOrPut(c) { mutableListOf() }.addAll(TileGeometry.clipLine(ring))
            }
        }
        val lines = mutableListOf<TileShape>()
        for ((c, parts) in byClass) {
            for (l in TileGeometry.mergeLines(parts)) {
                val s = TileGeometry.simplify(l, tol)
                if (s.size >= 2) lines += TileShape(c, s)
            }
        }
        return lines
    }

    /** Water and parks: one outer ring per polygon; holes are dropped. */
    private fun areas(
        tile: Map<String, List<TileFeature>>,
        level: MapLevel,
        tileY: Int,
        tol: Double,
    ): List<TileShape> {
        val unit = metresPerUnit(level.zoom, tileY)
        val unit2 = unit * unit
        val polys = mutableListOf<TileShape>()
        for ((layer, kind) in listOf("water" to WATER, "park" to PARK)) {
            if (kind == PARK && !level.parks) continue
            val rings = tile[layer].orEmpty().filter { it.type == POLYGON }.flatMap { it.rings }
            for (ring in rings.filter { TileGeometry.area2(it) > 0 }) {
                val clipped = TileGeometry.clipPolygon(ring)
                val big = clipped.size >= MIN_RING && TileGeometry.area2(clipped) / 2 * unit2 >= level.minWaterM2
                val s = if (big) TileGeometry.simplify(clipped + clipped[0], tol).dropLast(1) else emptyList()
                if (s.size >= MIN_RING) polys += TileShape(kind, s)
            }
        }
        return polys
    }

    /** The chunk for [shapes] in [frame]: origin at the tile's south-west corner. */
    fun chunk(
        shapes: TileShapes,
        frame: RideFrame,
        epoch: Int,
        dataVersion: Long,
        provenance: Provenance,
    ): Chunk {
        val level = shapes.level
        val z = level.zoom
        val q = level.quantumM
        val project = { u: Double, v: Double ->
            val (lat, lon) = tileToLatLon(z, shapes.tileX, shapes.tileY, u, v)
            frame.project(lat, lon)
        }
        val sw = project(0.0, EXTENT)
        val ne = project(EXTENT, 0.0)
        val ox = roundHalfUp(sw.x)
        val oy = roundHalfUp(sw.y)
        val extent = ceil(max(ne.x - sw.x, ne.y - sw.y) / q).toInt()
        val quantize = { pts: List<TilePoint> ->
            val out = mutableListOf<Point>()
            for (p in pts) {
                val l = project(p.x, p.y)
                val qp = Point(roundHalfUp((l.x - ox) / q), roundHalfUp((l.y - oy) / q))
                if (out.isEmpty() || out.last() != qp) out += qp
            }
            out
        }
        val lines = mutableListOf<Shape>()
        for (s in shapes.lines) {
            val qp = quantize(s.points)
            var start = 0
            while (start < qp.size - 1) {
                val piece = qp.subList(start, minOf(start + MAX_LINE_POINTS, qp.size))
                if (piece.size >= 2) lines += Shape(s.kind, piece.toList())
                start += MAX_LINE_POINTS - 1
            }
        }
        val polys = mutableListOf<Shape>()
        for (s in shapes.polys) {
            var qr: List<Point> = quantize(s.points)
            if (qr.size > 1 && qr.first() == qr.last()) qr = qr.dropLast(1)
            if (qr.size >= MIN_RING &&
                abs(area2(qr)) / 2 >= MIN_POLY_QUANTA2
            ) {
                polys += Shape(s.kind, qr.take(MAX_LINE_POINTS))
            }
        }
        return Chunk(
            key = ChunkKey(level.code, shapes.tileX.toLong(), shapes.tileY.toLong(), dataVersion),
            provenance = provenance,
            epoch = epoch,
            originXM = ox,
            originYM = oy,
            quantumDm = level.quantumDm,
            extent = extent,
            lines = lines,
            polys = polys,
        )
    }

    /** Latitude and longitude of a point in tile units (Web Mercator). */
    fun tileToLatLon(
        z: Int,
        tileX: Int,
        tileY: Int,
        u: Double,
        v: Double,
    ): Pair<Double, Double> {
        val n = (1 shl z).toDouble()
        val lon = (tileX + u / EXTENT) / n * FULL_TURN - HALF_TURN
        val lat = Math.toDegrees(atan(sinh(PI * (1 - 2 * (tileY + v / EXTENT) / n))))
        return lat to lon
    }

    /** Ground metres per tile unit at the tile's centre latitude. */
    fun metresPerUnit(
        z: Int,
        tileY: Int,
    ): Double {
        val (lat, _) = tileToLatLon(z, 0, tileY, 0.0, EXTENT / 2)
        return 2 * PI * A * cos(lat * DEG) / ((1 shl z) * EXTENT)
    }

    private fun roundHalfUp(v: Double): Int = floor(v + HALF).toInt()

    private fun area2(ring: List<Point>): Long {
        var sum = 0L
        for (i in ring.indices) {
            val a = ring[i]
            val b = ring[(i + 1) % ring.size]
            sum += a.x.toLong() * b.y - b.x.toLong() * a.y
        }
        return sum
    }

    private const val HALF = 0.5
}
