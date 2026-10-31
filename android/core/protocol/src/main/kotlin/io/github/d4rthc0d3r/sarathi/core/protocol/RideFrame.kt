package io.github.d4rthc0d3r.sarathi.core.protocol

import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.roundToLong
import kotlin.math.sin
import kotlin.math.sqrt
import kotlin.random.Random

/** A point in a ride frame: metres east ([x]) and north ([y]) of its origin. */
data class LocalPoint(
    val x: Double,
    val y: Double,
)

/**
 * The ride's own metric frame (ADR-0009): the dash only ever sees positions as metres from an
 * origin near the start, never latitude and longitude (ADR-0007). Equirectangular around the
 * origin with the WGS84 radii there, so the error stays under 0.1 % within [REANCHOR_M]; past
 * that the phone starts a new frame ([needsNewFrame]).
 */
class RideFrame private constructor(
    /** Sent in `frame`; random, so a new frame never matches one the dash still holds. */
    val epoch: Int,
    private val originLat: Double,
    private val originLon: Double,
) {
    private val metresPerRadLat: Double
    private val metresPerRadLon: Double

    init {
        val phi = originLat * DEG
        val s = sin(phi)
        val w = sqrt(1 - E2 * s * s)
        metresPerRadLat = A * (1 - E2) / (w * w * w) // meridian radius M
        metresPerRadLon = A / w * cos(phi) // prime vertical radius N, times cos(latitude)
    }

    fun project(
        lat: Double,
        lon: Double,
    ): LocalPoint {
        var dLon = lon - originLon
        if (dLon > HALF_TURN) dLon -= FULL_TURN
        if (dLon < -HALF_TURN) dLon += FULL_TURN
        return LocalPoint(dLon * DEG * metresPerRadLon, (lat - originLat) * DEG * metresPerRadLat)
    }

    /** True once [lat], [lon] is more than [REANCHOR_M] from the origin. */
    fun needsNewFrame(
        lat: Double,
        lon: Double,
    ): Boolean {
        val p = project(lat, lon)
        return p.x * p.x + p.y * p.y > REANCHOR_M * REANCHOR_M
    }

    companion object {
        /** A new frame starts beyond this distance from the origin (ADR-0009). */
        const val REANCHOR_M = 150_000.0

        private const val A = 6_378_137.0 // WGS84 semi-major axis
        private const val E2 = 6.694379990141317e-3 // WGS84 first eccentricity squared
        private const val DEG = PI / 180
        private const val HALF_TURN = 180.0
        private const val FULL_TURN = 360.0
        private const val ORIGIN_GRID = 100.0 // origin rounded to 0.01 degrees, about 1 km
        private const val MAX_EPOCH = 0xFFFF

        /** A frame with its origin near [lat], [lon], rounded to about 1 km. */
        fun near(
            lat: Double,
            lon: Double,
            random: Random = Random.Default,
        ) = RideFrame(
            random.nextInt(1, MAX_EPOCH + 1),
            (lat * ORIGIN_GRID).roundToLong() / ORIGIN_GRID,
            (lon * ORIGIN_GRID).roundToLong() / ORIGIN_GRID,
        )
    }
}
