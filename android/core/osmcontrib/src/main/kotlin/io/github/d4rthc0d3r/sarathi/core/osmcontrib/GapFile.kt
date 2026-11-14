package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint
import java.util.Locale

/** What became of a gap (ADR-0012 §3, §4). */
enum class GapStatus {
    /** Found on the map tiles, not yet confirmed with current OSM (Overpass couldn't be reached). */
    UNCHECKED,

    /** Confirmed: waiting for the rider. */
    OPEN,

    /** Skipped: hidden now; the same place found on a later ride is offered again. */
    SKIPPED,

    /** Dismissed for good: never offered again. */
    DISMISSED,

    /** The rider uploaded the GPS trace ([StoredGap.ref]: the trace's id). */
    TRACE_SENT,

    /** The rider added an OSM note ([StoredGap.ref]: the note's id). */
    NOTE_SENT,
}

/** A gap of one ride's track, and its status. */
data class StoredGap(
    val track: String,
    val gap: Gap,
    val status: GapStatus,
    /** What OSM gave back for it: a trace or note id. */
    val ref: String? = null,
) {
    /** Unique among the rider's gaps: the track and when the gap started. */
    val id: String get() = "$track@${gap.startUtcMs}"
}

/**
 * One track's gaps on the phone: a line per gap, `status;start;end;length;lat,lon lat,lon …;ref`
 * (the last field only once OSM gave one back). Malformed lines are skipped.
 */
object GapFile {
    private const val FIELDS = 5
    private val REF = Regex("[0-9]{1,15}")
    private const val STATUS = 0
    private const val START = 1
    private const val END = 2
    private const val LENGTH = 3
    private const val POINTS = 4

    fun line(g: StoredGap): String =
        listOf(
            g.status.name,
            g.gap.startUtcMs,
            g.gap.endUtcMs,
            String.format(Locale.ROOT, "%.0f", g.gap.lengthM),
            g.gap.points.joinToString(" ") { String.format(Locale.ROOT, "%.6f,%.6f", it.lat, it.lng) },
        ).plus(listOfNotNull(g.ref)).joinToString(";")

    fun read(
        track: String,
        lines: Sequence<String>,
    ): List<StoredGap> = lines.mapNotNull { parse(track, it) }.toList()

    private fun parse(
        track: String,
        line: String,
    ): StoredGap? {
        val f = line.split(';').takeIf { it.size == FIELDS || it.size == FIELDS + 1 } ?: return null
        val status = GapStatus.entries.firstOrNull { it.name == f[STATUS] }
        val times = f[START].toLongOrNull()?.let { a -> f[END].toLongOrNull()?.let { b -> a to b } }
        val length = f[LENGTH].toDoubleOrNull()
        val points =
            f[POINTS].split(' ').mapNotNull { pair ->
                val (lat, lng) = pair.split(',').takeIf { it.size == 2 } ?: return@mapNotNull null
                lat.toDoubleOrNull()?.let { a -> lng.toDoubleOrNull()?.let { b -> GeoPoint(a, b) } }
            }
        val gap =
            if (times != null && length != null &&
                points.size >= 2
            ) {
                Gap(points, length, times.first, times.second)
            } else {
                null
            }
        val ref = f.getOrNull(FIELDS)?.takeIf { it.matches(REF) }
        return if (status != null && gap != null) StoredGap(track, gap, status, ref) else null
    }
}
