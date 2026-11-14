package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import io.github.d4rthc0d3r.sarathi.core.mapdata.ChunkMaker
import io.github.d4rthc0d3r.sarathi.core.mapdata.Corridor
import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint
import io.github.d4rthc0d3r.sarathi.core.mapdata.MapLevel
import io.github.d4rthc0d3r.sarathi.core.mapdata.TileId
import io.github.d4rthc0d3r.sarathi.core.mapdata.TileShapes

/** The detailed map tiles a track needs, and their OSM roads as positions (ADR-0012). */
object OsmTiles {
    /** The L0 tiles under [points] and the 8 around each: a road just over a tile edge counts too. */
    fun around(points: List<GeoPoint>): List<TileId> {
        val out = LinkedHashSet<TileId>()
        for (p in points) {
            val (x, y) = Corridor.tileAt(MapLevel.L0.zoom, p)
            for (dy in -1..1) for (dx in -1..1) out += TileId(MapLevel.L0, x + dx, y + dy)
        }
        return out.toList()
    }

    /** The roads of [tiles] that may be used: L0, OSM provenance only ([RoadIndex.isOsm]). */
    fun roads(tiles: List<TileShapes>): List<List<GeoPoint>> =
        usable(tiles).flatMap { t ->
            t.lines.map { line ->
                line.points.map { pt ->
                    val (lat, lon) = ChunkMaker.tileToLatLon(t.level.zoom, t.tileX, t.tileY, pt.x, pt.y)
                    GeoPoint(lat, lon)
                }
            }
        }

    internal fun usable(tiles: List<TileShapes>) =
        tiles.filter {
            it.level == MapLevel.L0 &&
                RoadIndex.isOsm(it.provenance)
        }
}
