package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import java.util.Locale

/**
 * A ride's track on the phone (ADR-0012): a header line, then one fix per line,
 * `time,lat,lon,accuracy,speed` (speed empty when unknown). Lines are appended as fixes come, so
 * a ride cut short leaves a usable file; a broken last line is skipped when reading.
 */
object TrackFile {
    const val HEADER = "sarathi-track 1"
    private const val FIELDS = 5
    private const val TIME = 0
    private const val LAT = 1
    private const val LON = 2
    private const val ACCURACY = 3
    private const val SPEED = 4

    fun line(fix: RawGpsFix): String =
        String.format(
            Locale.ROOT,
            "%d,%.6f,%.6f,%.1f,%s",
            fix.timeUtcMs,
            fix.lat,
            fix.lon,
            fix.accuracyM,
            fix.speedMps?.let { String.format(Locale.ROOT, "%.1f", it) } ?: "",
        )

    /** The fixes in [lines]; empty if the header isn't ours. Malformed lines are skipped. */
    fun read(lines: Sequence<String>): List<RawGpsFix> {
        val it = lines.iterator()
        if (!it.hasNext() || it.next() != HEADER) return emptyList()
        return it.asSequence().mapNotNull(::parse).toList()
    }

    private fun parse(line: String): RawGpsFix? {
        val f =
            line.split(',').takeIf {
                it.size == FIELDS && (it[SPEED].isEmpty() || it[SPEED].toFloatOrNull() != null)
            } ?: return null
        val time = f[TIME].toLongOrNull()
        val position = f[LAT].toDoubleOrNull()?.let { lat -> f[LON].toDoubleOrNull()?.let { lon -> lat to lon } }
        val accuracy = f[ACCURACY].toFloatOrNull()
        // Only raw satellite fixes are ever written.
        return if (time != null && position != null && accuracy != null) {
            val (lat, lon) = position
            RawGpsFix.fromProvider(RawGpsFix.GNSS_PROVIDER, lat, lon, time, accuracy, f[SPEED].toFloatOrNull())
        } else {
            null
        }
    }

    /** The file name of a track started at [startUtcMs]. */
    fun name(startUtcMs: Long): String = "track-$startUtcMs.csv"

    /** When the track in [name] started, or null if it isn't a track file's name. */
    fun startOf(name: String): Long? =
        name
            .removePrefix("track-")
            .removeSuffix(".csv")
            .takeIf {
                name.startsWith("track-") && name.endsWith(".csv")
            }?.toLongOrNull()

    /** Tracks are kept this long, or until their gaps are handled (ADR-0012). */
    const val KEEP_DAYS = 30
    private const val MS_PER_DAY = 24L * 60 * 60 * 1000

    /** The track files among [names] older than [KEEP_DAYS] at [nowUtcMs]. */
    fun expired(
        names: List<String>,
        nowUtcMs: Long,
    ): List<String> = names.filter { n -> startOf(n)?.let { nowUtcMs - it > KEEP_DAYS * MS_PER_DAY } == true }

    /** The length of [fixes] in metres. */
    fun lengthM(fixes: List<RawGpsFix>): Double = fixes.zipWithNext { a, b -> distanceM(a, b) }.sum()
}
