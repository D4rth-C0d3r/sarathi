package io.github.d4rthc0d3r.sarathi.maps

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.location.LocationManager
import android.os.Build
import io.github.d4rthc0d3r.sarathi.core.maps.LatLng

/** The phone's last known position, to bias search results nearby; null without permission. */
object LastLocation {
    fun get(context: Context): LatLng? {
        val coarse = Manifest.permission.ACCESS_COARSE_LOCATION
        if (context.checkSelfPermission(coarse) != PackageManager.PERMISSION_GRANTED) return null
        val manager = context.getSystemService(LocationManager::class.java)
        val providers =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                listOf(LocationManager.FUSED_PROVIDER, LocationManager.NETWORK_PROVIDER, LocationManager.GPS_PROVIDER)
            } else {
                listOf(LocationManager.NETWORK_PROVIDER, LocationManager.GPS_PROVIDER)
            }
        val best =
            providers
                .mapNotNull { runCatching { manager.getLastKnownLocation(it) }.getOrNull() }
                .maxByOrNull { it.time }
        return best?.let { LatLng(it.latitude, it.longitude) }
    }
}
