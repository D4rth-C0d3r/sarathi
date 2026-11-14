package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint

/** A stretch of the rider's track where OSM has no road (ADR-0012). */
data class Gap(
    /** The track every [GapFinder.STEP_M], from the gap's start to its end. */
    val points: List<GeoPoint>,
    val lengthM: Double,
    val startUtcMs: Long,
    val endUtcMs: Long,
) {
    val middle: GeoPoint get() = points[points.size / 2]
}

/**
 * Finds gaps after a ride (ADR-0012 §3): the track is resampled every [STEP_M], never across
 * missing fixes; a gap is a run of at least [MIN_GAP_M] whose samples are all more than [NEAR_M]
 * from any road. Where there is no map data the track is unknown, never a gap.
 */
object GapFinder {
    const val STEP_M = 10.0
    const val NEAR_M = 25.0
    const val MIN_GAP_M = 150.0

    /** Fixes further apart than this aren't joined: the track has a hole there. */
    const val MAX_FIX_GAP_MS = 5_000L
    const val MAX_FIX_GAP_M = 150.0

    private enum class Where { ON_ROAD, OFF_ROAD, UNKNOWN }

    private class Sample(
        val at: GeoPoint,
        val timeUtcMs: Long,
    )

    /** The gaps of [track] against [roads]: candidates, still to be confirmed with current OSM. */
    fun find(
        track: List<RawGpsFix>,
        roads: RoadIndex,
    ): List<Gap> = pieces(track).flatMap { gaps(resample(it), roads) }

    /** What is left of [gap] against current OSM data: none, the gap, or parts of it. */
    fun confirm(
        gap: Gap,
        current: RoadIndex,
    ): List<Gap> {
        val samples =
            gap.points.mapIndexed { i, p ->
                val f = if (gap.points.size > 1) i.toDouble() / (gap.points.size - 1) else 0.0
                Sample(p, gap.startUtcMs + ((gap.endUtcMs - gap.startUtcMs) * f).toLong())
            }
        return gaps(samples, current)
    }

    private fun pieces(track: List<RawGpsFix>): List<List<RawGpsFix>> {
        val out = mutableListOf<MutableList<RawGpsFix>>()
        for (fix in track) {
            val last = out.lastOrNull()?.last()
            val joined =
                last != null &&
                    fix.timeUtcMs - last.timeUtcMs in 1..MAX_FIX_GAP_MS &&
                    distanceM(last, fix) <= MAX_FIX_GAP_M
            if (joined) out.last() += fix else out += mutableListOf(fix)
        }
        return out
    }

    private fun resample(piece: List<RawGpsFix>): List<Sample> {
        val out = mutableListOf(Sample(GeoPoint(piece[0].lat, piece[0].lon), piece[0].timeUtcMs))
        var carried = 0.0 // metres walked since the last sample
        for ((a, b) in piece.zipWithNext()) {
            val d = distanceM(a, b)
            var at = STEP_M - carried
            while (at <= d) {
                val f = at / d
                out +=
                    Sample(
                        GeoPoint(a.lat + (b.lat - a.lat) * f, a.lon + (b.lon - a.lon) * f),
                        a.timeUtcMs + ((b.timeUtcMs - a.timeUtcMs) * f).toLong(),
                    )
                at += STEP_M
            }
            carried = d - (at - STEP_M)
        }
        return out
    }

    private fun gaps(
        samples: List<Sample>,
        roads: RoadIndex,
    ): List<Gap> {
        val out = mutableListOf<Gap>()
        var run = mutableListOf<Sample>()

        fun close() {
            val length = (run.size - 1) * STEP_M
            if (run.size > 1 && length >= MIN_GAP_M) {
                out += Gap(run.map { it.at }, length, run.first().timeUtcMs, run.last().timeUtcMs)
            }
            run = mutableListOf()
        }
        for (s in samples) {
            if (where(s.at, roads) == Where.OFF_ROAD) run += s else close()
        }
        close()
        return out
    }

    private fun where(
        p: GeoPoint,
        roads: RoadIndex,
    ): Where =
        when {
            !roads.covered(p) -> Where.UNKNOWN
            roads.distanceM(p) <= NEAR_M || roads.insideArea(p) -> Where.ON_ROAD
            else -> Where.OFF_ROAD
        }
}
