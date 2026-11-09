package io.github.d4rthc0d3r.sarathi.core.maps

/**
 * Fixes along a route at a steady speed, for testing guidance at the desk and the simulated drive
 * (step F8). A [Detour] pushes the fixes sideways off the line for a stretch, to force a reroute.
 */
class RouteSimulator(
    private val points: List<LatLng>,
    private val speedMps: Double,
    private val intervalMs: Long = 1_000,
    private val startMs: Long = 0,
    private val detour: Detour? = null,
) {
    /** Leaves the route [offsetM] to the right, from [atM] along it, for [lengthM]. */
    data class Detour(
        val atM: Double,
        val lengthM: Double,
        val offsetM: Double,
    )

    private val cumulative = points.zipWithNext().runningFold(0.0) { acc, (a, b) -> acc + Geo.distanceM(a, b) }

    val lengthM: Double get() = cumulative.lastOrNull() ?: 0.0

    fun fixes(): Sequence<Fix> =
        sequence {
            if (points.size < 2) return@sequence
            val stepM = speedMps * intervalMs / MS_PER_S
            var d = 0.0
            var t = startMs
            while (true) {
                yield(fixAt(d.coerceAtMost(lengthM), t))
                if (d >= lengthM) break
                d += stepM
                t += intervalMs
            }
        }

    private fun fixAt(
        d: Double,
        timeMs: Long,
    ): Fix {
        val i = cumulative.indexOfLast { it <= d }.coerceIn(0, points.size - 2)
        val a = points[i]
        val b = points[i + 1]
        val seg = cumulative[i + 1] - cumulative[i]
        val f = if (seg > 0) (d - cumulative[i]) / seg else 0.0
        val bearing = Geo.bearingDeg(a, b)
        var at = LatLng(a.lat + (b.lat - a.lat) * f, a.lng + (b.lng - a.lng) * f)
        if (detour != null && d >= detour.atM && d < detour.atM + detour.lengthM) {
            at = Geo.offset(at, bearing + RIGHT_ANGLE, detour.offsetM)
        }
        return Fix(at, timeMs, speedMps, bearing)
    }

    private companion object {
        const val MS_PER_S = 1000.0
        const val RIGHT_ANGLE = 90.0
    }
}
