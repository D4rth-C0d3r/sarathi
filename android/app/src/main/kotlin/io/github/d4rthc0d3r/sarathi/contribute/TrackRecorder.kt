package io.github.d4rthc0d3r.sarathi.contribute

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Build
import android.os.Looper
import android.util.Log
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.RawGpsFix
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.TrackFile
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.TrackFilter
import java.io.BufferedWriter
import java.io.IOException

/**
 * Records the ride's raw GPS track for the OSM contribution helper (ADR-0012): satellite fixes
 * only, in its own listener (the dash's position uses the fused provider, which mixes in Google's
 * Wi-Fi and cell positions). Mock locations are left out. Runs while the dash is connected and the
 * helper is on; the file is opened at the first fix kept.
 */
class TrackRecorder(
    private val context: Context,
) : LocationListener {
    private val manager = context.getSystemService(LocationManager::class.java)
    private var filter = TrackFilter()
    private var writer: BufferedWriter? = null
    private var unflushed = 0
    private var running = false

    @SuppressLint("MissingPermission") // checked just above the request
    fun start() {
        if (running) return
        if (context.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            Log.w(TAG, "no track: location not allowed")
            return
        }
        TrackStorage.purgeExpired(context)
        filter = TrackFilter()
        manager.requestLocationUpdates(LocationManager.GPS_PROVIDER, INTERVAL_MS, 0f, this, Looper.getMainLooper())
        running = true
        Log.i(TAG, "recording the GPS track")
    }

    fun stop() {
        if (!running) return
        manager.removeUpdates(this)
        close()
        running = false
        Log.i(TAG, "track recording stopped")
    }

    override fun onLocationChanged(location: Location) {
        val fix = location.takeUnless { it.mock() }?.toRawFix()?.takeIf(filter::accept) ?: return
        try {
            val w = writer ?: TrackStorage.newTrack(context, fix.timeUtcMs).also { writer = it }
            w.write(TrackFile.line(fix))
            w.newLine()
            if (++unflushed >= FLUSH_EVERY) {
                w.flush()
                unflushed = 0
            }
        } catch (e: IOException) {
            Log.w(TAG, "track not written: ${e.message}")
            close()
        }
    }

    private fun close() {
        runCatching { writer?.close() }
        writer = null
        unflushed = 0
    }

    private fun Location.toRawFix(): RawGpsFix? =
        RawGpsFix.fromProvider(
            provider,
            latitude,
            longitude,
            time,
            if (hasAccuracy()) accuracy else null,
            if (hasSpeed()) speed else null,
        )

    private fun Location.mock(): Boolean =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            isMock
        } else {
            @Suppress("DEPRECATION") // isMock is API 31
            isFromMockProvider
        }

    private companion object {
        const val TAG = "Contribute"
        const val INTERVAL_MS = 1_000L
        const val FLUSH_EVERY = 10
    }
}
