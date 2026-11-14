package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint

/**
 * What of a ride's track goes to OSM for one gap (ADR-0012 §4): the stretch around the gap, less
 * every fix near where the ride started or ended or near a saved place (the privacy cut).
 */
object TraceCut {
    /** Track before and after the gap that goes with it. */
    const val CONTEXT_MS = 60_000L

    /** The privacy cut's default distance; the rider can change it. */
    const val DEFAULT_CUT_M = 300.0

    /**
     * The fixes of [track] to upload for [gap]: within [CONTEXT_MS] of it, none within [cutM] of
     * the track's first or last fix or of any of [avoid].
     */
    fun forGap(
        track: List<RawGpsFix>,
        gap: Gap,
        avoid: List<GeoPoint>,
        cutM: Double = DEFAULT_CUT_M,
    ): List<RawGpsFix> {
        if (track.isEmpty()) return emptyList()
        val keepOut = avoid + listOf(track.first(), track.last()).map { GeoPoint(it.lat, it.lon) }
        val from = gap.startUtcMs - CONTEXT_MS
        val to = gap.endUtcMs + CONTEXT_MS
        return track.filter { f ->
            f.timeUtcMs in from..to &&
                keepOut.none { p -> RoadIndex.segmentDistanceM(GeoPoint(f.lat, f.lon), p, p) < cutM }
        }
    }
}
