package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint
import io.github.d4rthc0d3r.sarathi.core.mapdata.MapLevel
import io.github.d4rthc0d3r.sarathi.core.mapdata.TileId
import io.github.d4rthc0d3r.sarathi.core.mapdata.TilePoint
import io.github.d4rthc0d3r.sarathi.core.mapdata.TileShape
import io.github.d4rthc0d3r.sarathi.core.mapdata.TileShapes
import io.github.d4rthc0d3r.sarathi.core.protocol.Provenance
import kotlin.math.PI
import kotlin.math.floor
import kotlin.math.ln
import kotlin.math.tan

/** Test maps: roads as processed tiles, and tracks, near an open field (no real place). */
object TestMaps {
    const val LAT = 15.5
    const val LON = 73.8
    const val M_PER_DEG = 111_320.0
    private const val EXTENT = 4096.0

    /** A point [north] and [east] metres from (LAT, LON). */
    fun at(
        north: Double,
        east: Double,
    ) = GeoPoint(LAT + north / M_PER_DEG, LON + east / (M_PER_DEG * kotlin.math.cos(Math.toRadians(LAT))))

    /** The L0 tiles holding [lines], with those roads in them. */
    fun tiles(
        lines: List<List<GeoPoint>>,
        provenance: Provenance = Provenance.OSM_OPENFREEMAP,
        level: MapLevel = MapLevel.L0,
        extra: List<GeoPoint> = emptyList(),
    ): List<TileShapes> {
        val z = level.zoom
        val ids = (lines.flatten() + extra).map { tileOf(z, it).first }.toSet()
        return ids.map { id ->
            val shapes =
                lines.map { line ->
                    TileShape(MINOR, line.map { p -> unitsIn(id, p, z) })
                }
            TileShapes(TileId(level, id.first, id.second), shapes, emptyList(), provenance, VERSION)
        }
    }

    // Tile units of p in tile [id] (may be outside 0..EXTENT where the line leaves the tile).
    private fun unitsIn(
        id: Pair<Int, Int>,
        p: GeoPoint,
        z: Int,
    ): TilePoint {
        val (fx, fy) = fractional(z, p)
        return TilePoint((fx - id.first) * EXTENT, (fy - id.second) * EXTENT)
    }

    private fun fractional(
        z: Int,
        p: GeoPoint,
    ): Pair<Double, Double> {
        val n = (1 shl z).toDouble()
        val x = (p.lng + 180) / 360 * n
        val r = Math.toRadians(p.lat)
        val y = (1 - ln(tan(r) + 1 / kotlin.math.cos(r)) / PI) / 2 * n
        return x to y
    }

    private fun tileOf(
        z: Int,
        p: GeoPoint,
    ): Pair<Pair<Int, Int>, Pair<Double, Double>> {
        val (x, y) = fractional(z, p)
        return (floor(x).toInt() to floor(y).toInt()) to ((x - floor(x)) * EXTENT to (y - floor(y)) * EXTENT)
    }

    /** A ride along [points] at about [speedMps], one fix a second. */
    fun ride(
        points: List<GeoPoint>,
        speedMps: Double = 10.0,
        startMs: Long = 1_700_000_000_000,
    ): List<RawGpsFix> {
        val out = mutableListOf<RawGpsFix>()
        var t = startMs
        for ((a, b) in points.zipWithNext()) {
            val d = RoadIndex.segmentDistanceM(a, b, b)
            val n = maxOf(1, (d / speedMps).toInt())
            for (i in 0 until n) {
                val f = i.toDouble() / n
                out += fix(GeoPoint(a.lat + (b.lat - a.lat) * f, a.lng + (b.lng - a.lng) * f), t)
                t += 1000
            }
        }
        out += fix(points.last(), t)
        return out
    }

    private fun fix(
        p: GeoPoint,
        t: Long,
    ) = RawGpsFix.fromProvider("gps", p.lat, p.lng, t, 5f, 10f)!!

    private const val MINOR = 5
    private const val VERSION = 20261101L
}
