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
import io.github.d4rthc0d3r.sarathi.core.protocol.Fix
import io.github.d4rthc0d3r.sarathi.core.protocol.FrameEpoch
import io.github.d4rthc0d3r.sarathi.core.protocol.PoseBuilder
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

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

    override fun onLocationChanged(location: Location) {
        LinkRepository.update {
            it.copy(
                locationFixes = it.locationFixes + 1,
                lastFixAtMs = SystemClock.elapsedRealtime(),
            )
        }
        val pose = builder.pose(location.toFix())
        if (builder.frameChanged) frameSent = false
        if (!dash.ready.value) return
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

    private companion object {
        const val TAG = "RideLink"
        const val INTERVAL_MS = 1_000L
        const val NANOS_PER_MS = 1_000_000L
    }
}
