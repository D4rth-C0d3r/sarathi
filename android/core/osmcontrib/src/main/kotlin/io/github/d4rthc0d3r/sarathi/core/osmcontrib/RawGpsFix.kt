package io.github.d4rthc0d3r.sarathi.core.osmcontrib

/**
 * One fix of the rider's own GPS track (ADR-0012). Only satellite fixes make one: never the fused
 * provider (which mixes in Google's Wi-Fi and cell positions), a position snapped by a
 * navigation SDK, or route geometry. [fromProvider] is the only way in from outside this module.
 */
@ConsistentCopyVisibility
data class RawGpsFix internal constructor(
    val lat: Double,
    val lon: Double,
    /** UTC, milliseconds since the epoch: GPX needs real timestamps. */
    val timeUtcMs: Long,
    val accuracyM: Float,
    val speedMps: Float?,
) {
    companion object {
        /** Android's `LocationManager.GPS_PROVIDER`. */
        const val GNSS_PROVIDER = "gps"
        private const val MAX_LAT = 90.0
        private const val MAX_LON = 180.0

        /** A fix from [provider], or null unless it's a satellite fix with an accuracy. */
        @Suppress("LongParameterList") // one fix's fields, as Android's Location has them
        fun fromProvider(
            provider: String?,
            lat: Double,
            lon: Double,
            timeUtcMs: Long,
            accuracyM: Float?,
            speedMps: Float?,
        ): RawGpsFix? {
            val valid = lat in -MAX_LAT..MAX_LAT && lon in -MAX_LON..MAX_LON && timeUtcMs > 0
            return if (provider == GNSS_PROVIDER && accuracyM != null && valid) {
                RawGpsFix(lat, lon, timeUtcMs, accuracyM, speedMps)
            } else {
                null
            }
        }
    }
}
