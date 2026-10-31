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

/**
 * Counts location fixes while the ride link runs, to show that location keeps working with the
 * screen off and the phone in Doze (CLAUDE.md, hard rules). The real position pipeline (pose
 * to the dash) comes with navigation. Android's own LocationManager: no Play services.
 */
class LocationCheck(
    private val context: Context,
) : LocationListener {
    private val manager = context.getSystemService(LocationManager::class.java)

    @SuppressLint("MissingPermission") // checked just above the request
    fun start(): Boolean {
        if (context.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            return false
        }
        // Fused (GPS, Wi-Fi, cell) where Android has it, so the check also works indoors.
        val provider =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && manager.hasProvider(LocationManager.FUSED_PROVIDER)) {
                LocationManager.FUSED_PROVIDER
            } else {
                LocationManager.GPS_PROVIDER
            }
        manager.requestLocationUpdates(provider, INTERVAL_MS, 0f, this, Looper.getMainLooper())
        return true
    }

    fun stop() = manager.removeUpdates(this)

    override fun onLocationChanged(location: Location) {
        LinkRepository.update {
            it.copy(
                locationFixes = it.locationFixes + 1,
                lastFixAtMs = SystemClock.elapsedRealtime(),
            )
        }
        val fixes = LinkRepository.status.value.locationFixes
        if (fixes % LOG_EVERY == 0) Log.d(TAG, "$fixes location fixes")
    }

    private companion object {
        const val INTERVAL_MS = 1_000L
        const val LOG_EVERY = 30
        const val TAG = "RideLink"
    }
}
