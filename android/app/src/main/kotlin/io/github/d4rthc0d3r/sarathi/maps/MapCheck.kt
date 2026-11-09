package io.github.d4rthc0d3r.sarathi.maps

import android.content.Context
import android.os.SystemClock
import io.github.d4rthc0d3r.sarathi.core.mapdata.ChunkMaker
import io.github.d4rthc0d3r.sarathi.core.mapdata.MapLevel
import io.github.d4rthc0d3r.sarathi.core.mapdata.TileId
import io.github.d4rthc0d3r.sarathi.core.protocol.BinaryCodec
import io.github.d4rthc0d3r.sarathi.core.protocol.RideFrame
import java.util.Locale
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.ln
import kotlin.math.tan

/**
 * Desk check for the map pipeline (step G2, debug builds): the tiles around the phone at both
 * levels, where each came from, what it holds and the chunk it makes.
 */
object MapCheck {
    suspend fun run(
        context: Context,
        offlineOnly: Boolean,
    ): String {
        val here = LastLocation.get(context) ?: return "No location yet: turn location on and try again."
        val registry = MapsRegistry.get(context)
        val data = if (offlineOnly) registry.offlineMapData(context) else registry.mapData
        val frame = RideFrame.near(here.lat, here.lng)
        val lines = mutableListOf<String>()
        lines += "Offline region: " + (registry.offlineMap.dataVersion()?.toString() ?: "none in this build")
        for (level in MapLevel.entries) {
            val (x, y) = tileAt(level.zoom, here.lat, here.lng)
            val start = SystemClock.elapsedRealtime()
            val result = data.shapes(TileId(level, x, y))
            val ms = SystemClock.elapsedRealtime() - start
            lines +=
                if (result == null) {
                    "L${level.code} $x/$y: no data ($ms ms)"
                } else {
                    val s = result.shapes
                    val bytes = BinaryCodec.encode(ChunkMaker.chunk(s, frame, 1)).size
                    "L${level.code} $x/$y: ${result.origin.name.lowercase()}, ${s.provenance.name.lowercase()} " +
                        "${s.dataVersion}, ${s.lines.size} roads, ${s.polys.size} areas, " +
                        "chunk ${String.format(Locale.ROOT, "%.1f", bytes / KB)} KB ($ms ms)"
                }
        }
        lines += "Cache: ${(registry.mapCache.size() / KB).toLong()} KB"
        return lines.joinToString("\n")
    }

    /** The Web Mercator tile holding a point. */
    fun tileAt(
        zoom: Int,
        lat: Double,
        lng: Double,
    ): Pair<Int, Int> {
        val n = (1 shl zoom).toDouble()
        val r = lat * PI / HALF_TURN
        val x = floor((lng + HALF_TURN) / FULL_TURN * n).toInt()
        val y = floor((1 - ln(tan(r) + 1 / cos(r)) / PI) / 2 * n).toInt()
        return x to y
    }

    private const val KB = 1024.0
    private const val HALF_TURN = 180.0
    private const val FULL_TURN = 360.0
}
