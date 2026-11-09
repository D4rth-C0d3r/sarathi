package io.github.d4rthc0d3r.sarathi.core.mapdata

import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.ln
import kotlin.math.sqrt
import kotlin.math.tan

/** A position on Earth, for planning on the phone (the dash never gets these, ADR-0009). */
data class GeoPoint(
    val lat: Double,
    val lng: Double,
)

/**
 * Which tiles the dash needs during a ride (ADR-0009 "BLE budget", phone side), most urgent
 * first, without repeats:
 * 1. L0 around the rider (the city zoom), and L1 around the rider (the highway zoom)
 * 2. along the next [L0_AHEAD_M] of route: L0 within [L0_HALF_WIDTH_M] of it, with L1
 * 3. L1 along the rest of the route
 */
object Corridor {
    const val L0_AROUND_M = 1_500.0
    const val L1_AROUND_M = 5_000.0
    const val L0_AHEAD_M = 10_000.0
    const val L0_HALF_WIDTH_M = 700.0
    const val L1_HALF_WIDTH_M = 4_000.0
    private const val STEP_M = 250.0
    private const val METRES_PER_DEG = 111_320.0
    private const val DEG = PI / 180
    private const val HALF_TURN = 180.0
    private const val FULL_TURN = 360.0

    fun plan(
        route: List<GeoPoint>,
        rider: GeoPoint,
    ): List<TileId> {
        val out = LinkedHashSet<TileId>()
        out += around(MapLevel.L0, rider, L0_AROUND_M)
        out += around(MapLevel.L1, rider, L1_AROUND_M)
        val samples = ahead(route, rider)
        for ((along, p) in samples) {
            if (along > L0_AHEAD_M) break
            out += around(MapLevel.L0, p, L0_HALF_WIDTH_M)
            out += around(MapLevel.L1, p, L1_HALF_WIDTH_M)
        }
        for ((along, p) in samples) {
            if (along > L0_AHEAD_M) out += around(MapLevel.L1, p, L1_HALF_WIDTH_M)
        }
        return out.toList()
    }

    /** The tile holding a point at [zoom] (Web Mercator). */
    fun tileAt(
        zoom: Int,
        p: GeoPoint,
    ): Pair<Int, Int> {
        val n = (1 shl zoom).toDouble()
        val r = p.lat * DEG
        val x = floor((p.lng + HALF_TURN) / FULL_TURN * n).toInt()
        val y = floor((1 - ln(tan(r) + 1 / cos(r)) / PI) / 2 * n).toInt()
        return x to y
    }

    /** Tiles within [radiusM] of [p] (by bounding box), nearest first. */
    private fun around(
        level: MapLevel,
        p: GeoPoint,
        radiusM: Double,
    ): List<TileId> {
        val dLat = radiusM / METRES_PER_DEG
        val dLng = radiusM / (METRES_PER_DEG * cos(p.lat * DEG))
        val (x0, y0) = tileAt(level.zoom, GeoPoint(p.lat + dLat, p.lng - dLng)) // north-west
        val (x1, y1) = tileAt(level.zoom, GeoPoint(p.lat - dLat, p.lng + dLng)) // south-east
        val (cx, cy) = tileAt(level.zoom, p)
        val tiles = mutableListOf<TileId>()
        for (x in x0..x1) for (y in y0..y1) tiles += TileId(level, x, y)
        return tiles.sortedBy { (it.x - cx) * (it.x - cx) + (it.y - cy) * (it.y - cy) }
    }

    /** Points every [STEP_M] along the route from the point nearest the rider, with the distance. */
    private fun ahead(
        route: List<GeoPoint>,
        rider: GeoPoint,
    ): List<Pair<Double, GeoPoint>> {
        if (route.size < 2) return emptyList()
        val start = route.indices.minBy { metres(route[it], rider) }
        val out = mutableListOf(0.0 to route[start])
        var along = 0.0
        var next = STEP_M
        for (i in start until route.size - 1) {
            val a = route[i]
            val b = route[i + 1]
            val len = metres(a, b)
            while (len > 0 && next <= along + len) {
                val f = (next - along) / len
                out += next to GeoPoint(a.lat + (b.lat - a.lat) * f, a.lng + (b.lng - a.lng) * f)
                next += STEP_M
            }
            along += len
        }
        out += along to route.last()
        return out
    }

    private fun metres(
        a: GeoPoint,
        b: GeoPoint,
    ): Double {
        val dy = (b.lat - a.lat) * METRES_PER_DEG
        val dx = (b.lng - a.lng) * METRES_PER_DEG * cos((a.lat + b.lat) / 2 * DEG)
        return sqrt(dx * dx + dy * dy)
    }
}
