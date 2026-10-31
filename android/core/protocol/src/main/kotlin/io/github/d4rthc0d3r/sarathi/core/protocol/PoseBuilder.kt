package io.github.d4rthc0d3r.sarathi.core.protocol

import kotlin.math.hypot
import kotlin.math.roundToInt
import kotlin.math.roundToLong

/** A location fix, without Android types. Optional values are null when the fix lacks them. */
data class Fix(
    val lat: Double,
    val lon: Double,
    /** Monotonic time of the fix, ms. */
    val elapsedMs: Long,
    val speedMps: Float? = null,
    val bearingDeg: Float? = null,
    val accuracyM: Float? = null,
)

/**
 * Turns fixes into `pose` in a ride frame (protocol.md section 4, ADR-0009): starts a frame at
 * the first fix and a new one past 150 km ([frameChanged] tells the caller to send `frame`).
 * Speed is the GPS's own when it has one (Doppler, steady at a standstill), else distance over
 * time from the previous fix. One per ride link.
 */
class PoseBuilder(
    private val newFrame: (Double, Double) -> RideFrame = { lat, lon -> RideFrame.near(lat, lon) },
) {
    var frame: RideFrame? = null
        private set

    /** True after a [pose] call that started a frame; the caller sends `frame` first. */
    var frameChanged = false
        private set

    private var previous: Fix? = null
    private var previousPoint: LocalPoint? = null
    private var headingCdeg = 0

    fun pose(fix: Fix): Pose {
        val current = frame
        frameChanged = current == null || current.needsNewFrame(fix.lat, fix.lon)
        val f = if (frameChanged) newFrame(fix.lat, fix.lon).also { frame = it } else checkNotNull(current)
        if (frameChanged) previousPoint = null
        val p = f.project(fix.lat, fix.lon)
        fix.bearingDeg?.let { headingCdeg = (it * CDEG_PER_DEG).roundToInt().mod(FULL_TURN_CDEG) }
        val speed = fix.speedMps ?: derivedSpeed(fix, p)
        previous = fix
        previousPoint = p
        return Pose(
            epoch = f.epoch,
            tMs = fix.elapsedMs and U32_MAX,
            xDm = (p.x * DM_PER_M).roundToLong().toInt(),
            yDm = (p.y * DM_PER_M).roundToLong().toInt(),
            headingCdeg = headingCdeg,
            speedCms = (speed * CM_PER_M).roundToInt().coerceIn(0, U16_MAX),
            accM = fix.accuracyM?.roundToInt()?.coerceIn(0, MAX_KNOWN_ACC) ?: Pose.ACC_UNKNOWN,
        )
    }

    private fun derivedSpeed(
        fix: Fix,
        p: LocalPoint,
    ): Float {
        val before = previous
        val from = previousPoint
        val dt = if (before == null) 0.0 else (fix.elapsedMs - before.elapsedMs) / MS_PER_S
        return if (from == null || dt <= 0) 0f else (hypot(p.x - from.x, p.y - from.y) / dt).toFloat()
    }

    private companion object {
        const val CDEG_PER_DEG = 100
        const val FULL_TURN_CDEG = 36_000
        const val DM_PER_M = 10.0
        const val CM_PER_M = 100f
        const val MS_PER_S = 1000.0
        const val MAX_KNOWN_ACC = 254 // 255 means unknown
    }
}
