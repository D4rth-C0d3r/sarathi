package io.github.d4rthc0d3r.sarathi.core.maps

import kotlin.math.asin
import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * Traffic from every backend on one scale (ADR-0006): stretches along the route by metres from
 * its start, sorted, not overlapping, neighbours of the same level merged.
 */
object Traffic {
    /** Per-segment levels (Mappls's `congestion`) with each segment's length. */
    fun fromSegments(
        lengthsM: List<Double>,
        levels: List<TrafficLevel>,
    ): List<TrafficStretch> {
        var offset = 0.0
        val stretches =
            lengthsM.zip(levels).map { (length, level) ->
                TrafficStretch(offset, length, level).also { offset += length }
            }
        return merge(stretches)
    }

    /**
     * Google's stretches, given per leg relative to the leg's start; the gaps between them are
     * free-flowing (Google lists only slow, jam and unknown).
     */
    fun fromLegs(
        legLengthsM: List<Double>,
        legs: List<List<TrafficStretch>>,
    ): List<TrafficStretch> {
        val out = mutableListOf<TrafficStretch>()
        var legStart = 0.0
        legLengthsM.zip(legs).forEach { (legLength, stretches) ->
            var cursor = 0.0
            stretches.sortedBy { it.offsetM }.forEach { s ->
                if (s.offsetM > cursor) out += TrafficStretch(legStart + cursor, s.offsetM - cursor, TrafficLevel.FREE)
                val start = maxOf(s.offsetM, cursor)
                val end = minOf(s.offsetM + s.lengthM, legLength)
                if (end > start) out += TrafficStretch(legStart + start, end - start, s.level)
                cursor = maxOf(cursor, end)
            }
            if (legLength > cursor) out += TrafficStretch(legStart + cursor, legLength - cursor, TrafficLevel.FREE)
            legStart += legLength
        }
        return merge(out)
    }

    /** Mappls's congestion names; "severe" is a jam too, anything else unknown. */
    fun mapplsLevel(name: String?): TrafficLevel =
        when (name) {
            "low" -> TrafficLevel.FREE
            "moderate" -> TrafficLevel.SLOW
            "heavy", "severe" -> TrafficLevel.JAM
            else -> TrafficLevel.UNKNOWN
        }

    /** Metres at each level, for summaries. */
    fun totals(stretches: List<TrafficStretch>): Map<TrafficLevel, Double> =
        stretches.groupBy { it.level }.mapValues { (_, s) -> s.sumOf { it.lengthM } }

    private fun merge(stretches: List<TrafficStretch>): List<TrafficStretch> =
        stretches.filter { it.lengthM > 0 }.fold(mutableListOf()) { acc, s ->
            val last = acc.lastOrNull()
            if (last != null && last.level == s.level && s.offsetM <= last.offsetM + last.lengthM + EPSILON_M) {
                acc[acc.lastIndex] = last.copy(lengthM = s.offsetM + s.lengthM - last.offsetM)
            } else {
                acc += s
            }
            acc
        }

    private const val EPSILON_M = 0.5
}

object Geo {
    private const val EARTH_RADIUS_M = 6_371_008.8

    /** Great-circle distance between two points. */
    fun distanceM(
        a: LatLng,
        b: LatLng,
    ): Double {
        val dLat = Math.toRadians(b.lat - a.lat)
        val dLng = Math.toRadians(b.lng - a.lng)
        val h =
            sin(dLat / 2).let { it * it } +
                cos(Math.toRadians(a.lat)) * cos(Math.toRadians(b.lat)) * sin(dLng / 2).let { it * it }
        return 2 * EARTH_RADIUS_M * asin(sqrt(h))
    }

    fun lengthM(points: List<LatLng>): Double = points.zipWithNext().sumOf { (a, b) -> distanceM(a, b) }
}
