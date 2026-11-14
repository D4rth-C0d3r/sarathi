package io.github.d4rthc0d3r.sarathi.core.osmcontrib

/**
 * Which raw fixes go into a ride's track (ADR-0012): accurate ones, while moving, without jumps.
 * Standing still adds nothing to a road and would only record where the rider waits.
 */
class TrackFilter(
    private val maxAccuracyM: Float = MAX_ACCURACY_M,
    private val minSpeedMps: Float = MIN_SPEED_MPS,
    private val maxSpeedMps: Float = MAX_SPEED_MPS,
) {
    private var last: RawGpsFix? = null

    /** True if [fix] belongs in the track; it then becomes the fix later ones are checked against. */
    fun accept(fix: RawGpsFix): Boolean {
        val previous = last
        val dtS = previous?.let { (fix.timeUtcMs - it.timeUtcMs) / MS_PER_S }
        val implied = if (previous != null && dtS != null && dtS > 0) distanceM(previous, fix) / dtS else null
        val usable =
            fix.accuracyM <= maxAccuracyM &&
                (dtS == null || dtS > 0) && // not out of order or repeated
                (implied == null || implied <= maxSpeedMps) // a jump, not a ride
        if (!usable) return false
        last = fix
        val speed = fix.speedMps?.toDouble() ?: implied
        return speed != null && speed >= minSpeedMps
    }

    companion object {
        const val MAX_ACCURACY_M = 15f
        const val MIN_SPEED_MPS = 1.5f // 5.4 km/h
        const val MAX_SPEED_MPS = 70f // 252 km/h
        private const val MS_PER_S = 1000.0
    }
}
