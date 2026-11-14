package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint
import java.util.Locale

/**
 * Places the rider dismissed for good (ADR-0012 §3): a gap there is never offered again. Each is a
 * circle around a dismissed gap, kept on the phone as `lat,lon,radius` lines.
 */
class DismissedAreas(
    areas: List<Area> = emptyList(),
) {
    data class Area(
        val center: GeoPoint,
        val radiusM: Double,
    )

    private val list = areas.toMutableList()
    val areas: List<Area> get() = list

    /** True if most of [gap] lies in a dismissed place. */
    fun covers(gap: Gap): Boolean {
        val inside = gap.points.count { p -> list.any { distance(p, it.center) <= it.radiusM } }
        return inside * 2 > gap.points.size
    }

    /** Dismisses [gap]'s place: a circle through its ends, and a margin. */
    fun dismiss(gap: Gap): Area {
        val c = gap.middle
        val r = gap.points.maxOf { distance(it, c) } + MARGIN_M
        return Area(c, r).also { list += it }
    }

    fun lines(): List<String> =
        list.map { String.format(Locale.ROOT, "%.6f,%.6f,%.0f", it.center.lat, it.center.lng, it.radiusM) }

    companion object {
        const val MARGIN_M = 50.0
        private const val FIELDS = 3

        fun read(lines: Sequence<String>): DismissedAreas =
            DismissedAreas(
                lines
                    .mapNotNull { line ->
                        val f = line.split(',').takeIf { it.size == FIELDS }
                        val lat = f?.get(0)?.toDoubleOrNull()
                        val lng = f?.get(1)?.toDoubleOrNull()
                        val r = f?.get(2)?.toDoubleOrNull()
                        if (lat != null && lng != null && r != null) Area(GeoPoint(lat, lng), r) else null
                    }.toList(),
            )

        private fun distance(
            a: GeoPoint,
            b: GeoPoint,
        ) = RoadIndex.segmentDistanceM(a, b, b)
    }
}
