package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import io.github.d4rthc0d3r.sarathi.core.mapdata.ChunkMaker
import io.github.d4rthc0d3r.sarathi.core.mapdata.Corridor
import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint
import io.github.d4rthc0d3r.sarathi.core.mapdata.MapLevel
import io.github.d4rthc0d3r.sarathi.core.mapdata.TileShapes
import io.github.d4rthc0d3r.sarathi.core.protocol.Provenance
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.hypot
import kotlin.math.max

/**
 * OSM roads near a track, for finding gaps (ADR-0012). Built only from OpenStreetMap: map tiles
 * whose provenance is OSM ([fromTiles]), or current OSM data from Overpass ([fromOsm]). Roads sit
 * in cells of about 100 m, so a point only looks at the roads near it.
 */
class RoadIndex private constructor(
    private val cells: Map<Long, List<Segment>>,
    private val areas: List<List<GeoPoint>>,
    private val covers: (GeoPoint) -> Boolean,
) {
    private class Segment(
        val a: GeoPoint,
        val b: GeoPoint,
    )

    /** Whether there is map data at [p] at all: where there isn't, nothing can be called a gap. */
    fun covered(p: GeoPoint): Boolean = covers(p)

    /** Metres to the nearest road within [NEAR_LIMIT_M], else [Double.POSITIVE_INFINITY]. */
    fun distanceM(p: GeoPoint): Double {
        var best = Double.POSITIVE_INFINITY
        cells[cellOf(p.lat, p.lng)]?.forEach { best = minOf(best, segmentDistanceM(p, it.a, it.b)) }
        return if (best <= NEAR_LIMIT_M) best else Double.POSITIVE_INFINITY
    }

    /** Inside a car park or fuel station (from Overpass): riding there isn't a missing road. */
    fun insideArea(p: GeoPoint): Boolean = areas.any { inside(p, it) }

    companion object {
        /** The furthest a road is looked for: more than the gap rule's 25 m. */
        const val NEAR_LIMIT_M = 40.0
        private const val CELL_DEG = 0.001 // about 111 m of latitude
        private const val M_PER_DEG = 111_320.0
        private const val CELL_KEY = 1L shl 32
        private const val STEP_FRACTION = 0.5

        /**
         * Whether map data of this provenance may be used for OSM contributions. Exhaustive on
         * purpose: a new provenance doesn't compile until it is classified here, so a non-OSM
         * source can never slip in (ADR-0012).
         */
        fun isOsm(provenance: Provenance): Boolean =
            when (provenance) {
                Provenance.OSM_OPENFREEMAP -> true
                Provenance.OSM_PROTOMAPS_OFFLINE -> true
            }

        /**
         * Roads from processed map tiles: only the most detailed level (L0, every road class) and
         * only OSM provenance. A place counts as covered only inside such a tile.
         */
        fun fromTiles(tiles: List<TileShapes>): RoadIndex {
            val usable = tiles.filter { it.level == MapLevel.L0 && isOsm(it.provenance) }
            val lines =
                usable.flatMap { t ->
                    t.lines.map { line ->
                        line.points.map { pt ->
                            val (lat, lon) = ChunkMaker.tileToLatLon(t.level.zoom, t.tileX, t.tileY, pt.x, pt.y)
                            GeoPoint(lat, lon)
                        }
                    }
                }
            val held = usable.map { it.tileX to it.tileY }.toSet()
            return RoadIndex(index(lines), emptyList()) { p -> Corridor.tileAt(MapLevel.L0.zoom, p) in held }
        }

        /** Current OSM ways from Overpass around a gap: roads, and car park or fuel areas. */
        fun fromOsm(
            roads: List<List<GeoPoint>>,
            areas: List<List<GeoPoint>>,
        ): RoadIndex = RoadIndex(index(roads), areas) { true }

        private fun index(lines: List<List<GeoPoint>>): Map<Long, List<Segment>> {
            val cells = HashMap<Long, MutableList<Segment>>()
            for ((a, b) in lines.flatMap { it.zipWithNext() }) {
                val segment = Segment(a, b)
                cellsNear(a, b).forEach { cells.getOrPut(it) { mutableListOf() } += segment }
            }
            return cells
        }

        /**
         * The cells within a cell of the segment [a]-[b] (more than NEAR_LIMIT_M): its points every
         * half cell, each with the 8 cells around it.
         */
        private fun cellsNear(
            a: GeoPoint,
            b: GeoPoint,
        ): Set<Long> {
            val keys = HashSet<Long>()
            val steps = max(1, (degLength(a, b) / (CELL_DEG * STEP_FRACTION)).toInt())
            for (i in 0..steps) {
                val f = i.toDouble() / steps
                val lat = a.lat + (b.lat - a.lat) * f
                val lng = a.lng + (b.lng - a.lng) * f
                for (dy in -1..1) for (dx in -1..1) keys += cellOf(lat + dy * CELL_DEG, lng + dx * CELL_DEG)
            }
            return keys
        }

        private fun degLength(
            a: GeoPoint,
            b: GeoPoint,
        ) = hypot(b.lat - a.lat, b.lng - a.lng)

        private fun cellOf(
            lat: Double,
            lng: Double,
        ): Long = floor(lat / CELL_DEG).toLong() * CELL_KEY + floor(lng / CELL_DEG).toLong()

        /** Metres from [p] to the segment [a]-[b], on a local flat projection around [p]. */
        fun segmentDistanceM(
            p: GeoPoint,
            a: GeoPoint,
            b: GeoPoint,
        ): Double {
            val k = cos(Math.toRadians(p.lat)) * M_PER_DEG
            val ax = (a.lng - p.lng) * k
            val ay = (a.lat - p.lat) * M_PER_DEG
            val bx = (b.lng - p.lng) * k
            val by = (b.lat - p.lat) * M_PER_DEG
            val dx = bx - ax
            val dy = by - ay
            val len2 = dx * dx + dy * dy
            val t = if (len2 == 0.0) 0.0 else (-(ax * dx + ay * dy) / len2).coerceIn(0.0, 1.0)
            return hypot(ax + t * dx, ay + t * dy)
        }

        private fun inside(
            p: GeoPoint,
            ring: List<GeoPoint>,
        ): Boolean {
            var inside = false
            var j = ring.size - 1
            for (i in ring.indices) {
                val a = ring[i]
                val b = ring[j]
                if ((a.lat > p.lat) != (b.lat > p.lat) &&
                    p.lng < (b.lng - a.lng) * (p.lat - a.lat) / (b.lat - a.lat) + a.lng
                ) {
                    inside = !inside
                }
                j = i
            }
            return inside
        }
    }
}
