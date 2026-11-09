package io.github.d4rthc0d3r.sarathi.link

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Build
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import io.github.d4rthc0d3r.sarathi.core.ble.DashChannel
import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.protocol.Fix
import io.github.d4rthc0d3r.sarathi.core.protocol.FrameEpoch
import io.github.d4rthc0d3r.sarathi.core.protocol.PoseBuilder
import io.github.d4rthc0d3r.sarathi.core.protocol.RideFrame
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import io.github.d4rthc0d3r.sarathi.core.maps.Fix as MapsFix

/**
 * The phone's position for the dash, once a second: `frame` when a ride frame starts or the dash
 * reconnects, then `pose` (protocol.md section 4). The dash locks its settings from the speed in
 * it (ADR-0011) and, with navigation, draws the map from it. Positions leave the phone only in
 * the ride's frame (ADR-0009). Android's own LocationManager: no Play services.
 */
class PoseSender(
    private val context: Context,
    private val dash: DashChannel,
    private val scope: CoroutineScope,
) : LocationListener {
    private val manager = context.getSystemService(LocationManager::class.java)
    private val builder = PoseBuilder()
    private var frameSent = false
    private var job: Job? = null
    private var loggedKmh = -1.0

    private val _frame = MutableStateFlow<RideFrame?>(null)

    /** The ride's frame, once the first fix has started one; a new epoch when it re-anchors. */
    val frame: StateFlow<RideFrame?> = _frame.asStateFlow()

    private val _fixes = MutableSharedFlow<MapsFix>(extraBufferCapacity = FIX_BUFFER)

    /** Every fix, for guidance (ADR-0006), as it arrives. */
    val fixes: SharedFlow<MapsFix> = _fixes.asSharedFlow()

    @SuppressLint("MissingPermission") // checked just above the request
    fun start(): Boolean {
        if (context.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            return false
        }
        // Fused (GPS, Wi-Fi, cell) where Android has it; GPS otherwise.
        val provider =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && manager.hasProvider(LocationManager.FUSED_PROVIDER)) {
                LocationManager.FUSED_PROVIDER
            } else {
                LocationManager.GPS_PROVIDER
            }
        manager.requestLocationUpdates(provider, INTERVAL_MS, 0f, this, Looper.getMainLooper())
        job = scope.launch { dash.ready.collect { if (it) frameSent = false } } // a new connection gets the frame
        return true
    }

    fun stop() {
        manager.removeUpdates(this)
        job?.cancel()
    }

    private var lastPoseSentMs = 0L

    /** While a ride is simulated (step F8), the phone's own fixes are left out. */
    private var simulated = false

    fun simulate(on: Boolean) {
        simulated = on
    }

    /** A simulated fix, sent to the dash and guidance like a real one (step F8). */
    fun inject(fix: MapsFix) {
        if (simulated) {
            handle(
                Fix(
                    fix.at.lat,
                    fix.at.lng,
                    fix.timeMs,
                    fix.speedMps?.toFloat(),
                    fix.bearingDeg?.toFloat(),
                    fix.accuracyM.toFloat(),
                ),
                fix,
            )
        }
    }

    override fun onLocationChanged(location: Location) {
        LinkRepository.update {
            it.copy(
                locationFixes = it.locationFixes + 1,
                lastFixAtMs = SystemClock.elapsedRealtime(),
            )
        }
        if (!simulated) handle(location.toFix(), location.toMapsFix())
    }

    private fun handle(
        fix: Fix,
        mapsFix: MapsFix,
    ) {
        val pose = builder.pose(fix)
        if (builder.frameChanged) _frame.value = builder.frame
        _fixes.tryEmit(mapsFix)
        val kmh = pose.speedCms * CMS_TO_KMH
        if (kotlin.math.abs(kmh - loggedKmh) >= 1) {
            loggedKmh = kmh
            Log.d(TAG, "pose speed %.1f km/h (accuracy %d m)".format(kmh, pose.accM))
        }
        if (builder.frameChanged) frameSent = false
        // The protocol's 2 Hz while navigating at most; a fast simulation makes more fixes.
        val now = SystemClock.elapsedRealtime()
        if (!dash.ready.value || (frameSent && now - lastPoseSentMs < MIN_POSE_MS)) return
        lastPoseSentMs = now
        val withFrame = !frameSent
        frameSent = true
        scope.launch {
            runCatching {
                if (withFrame) dash.send(FrameEpoch(pose.epoch))
                dash.send(pose)
            }.onFailure {
                frameSent = false
                Log.w(TAG, "pose not sent: ${it.message}")
            }
        }
    }

    private fun Location.toFix() =
        Fix(
            lat = latitude,
            lon = longitude,
            elapsedMs = elapsedRealtimeNanos / NANOS_PER_MS,
            speedMps = if (hasSpeed()) speed else null,
            bearingDeg = if (hasBearing()) bearing else null,
            accuracyM = if (hasAccuracy()) accuracy else null,
        )

    private fun Location.toMapsFix() =
        MapsFix(
            at = LatLng(latitude, longitude),
            timeMs = elapsedRealtimeNanos / NANOS_PER_MS,
            speedMps = if (hasSpeed()) speed.toDouble() else null,
            bearingDeg = if (hasBearing()) bearing.toDouble() else null,
            accuracyM = if (hasAccuracy()) accuracy.toDouble() else UNKNOWN_ACCURACY_M,
        )

    private companion object {
        const val FIX_BUFFER = 8
        const val MIN_POSE_MS = 500L
        const val UNKNOWN_ACCURACY_M = 50.0
        const val TAG = "RideLink"
        const val INTERVAL_MS = 1_000L
        const val NANOS_PER_MS = 1_000_000L
        const val CMS_TO_KMH = 0.036
    }
}
