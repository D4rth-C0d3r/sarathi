package io.github.d4rthc0d3r.sarathi.link

import android.Manifest
import android.app.NotificationManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.location.LocationManager
import android.util.Log
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import androidx.core.location.LocationManagerCompat
import androidx.lifecycle.LifecycleService
import androidx.lifecycle.lifecycleScope
import io.github.d4rthc0d3r.sarathi.core.ble.DashGatt
import io.github.d4rthc0d3r.sarathi.core.ble.DashLink
import io.github.d4rthc0d3r.sarathi.core.ble.LinkState
import kotlinx.coroutines.launch

/**
 * The ride link: a foreground service that keeps the dash connected with the screen off and the
 * phone in Doze (ADR-0010). Types connectedDevice and, when Android allows it, location. What
 * flows over the link is [RideSession]'s.
 */
class RideLinkService : LifecycleService() {
    private var link: DashLink? = null
    private var startedByPresence = false
    private var session: RideSession? = null

    override fun onStartCommand(
        intent: Intent?,
        flags: Int,
        startId: Int,
    ): Int {
        super.onStartCommand(intent, flags, startId)
        return when (intent?.action) {
            ACTION_STOP -> stopLink()
            ACTION_PRESENCE_GONE -> if (startedByPresence) stopLink() else START_STICKY
            else -> startLink(fromPresence = intent?.getBooleanExtra(EXTRA_FROM_PRESENCE, false) == true)
        }
    }

    private fun startLink(fromPresence: Boolean): Int {
        val address = DashAssociation(this).address()
        if (address == null || !DashGatt.canConnect(this)) {
            Log.w(TAG, "not starting: ${if (address == null) "no dash paired" else "Nearby devices not allowed"}")
            stopSelf()
            return START_NOT_STICKY
        }
        if (link == null) {
            startedByPresence = fromPresence // a restart by Android (null intent) counts as the rider's
            val versionName = packageManager.getPackageInfo(packageName, 0).versionName ?: "0"
            val dash = DashLink(this, address, versionName, lifecycleScope, RideSession.caps(this)).also { it.start() }
            link = dash
            session = RideSession(this, dash, lifecycleScope).also { it.start() }
            lifecycleScope.launch { dash.state.collect { onLinkState(it) } }
        }
        // Every start: one from the app may now be allowed location that a background one wasn't.
        goForeground(link?.state?.value ?: LinkState.Waiting)
        return START_STICKY
    }

    /**
     * Foreground with connectedDevice, plus location when allowed: Android refuses location to a
     * service started from the background unless "Allow all the time" was granted (ADR-0010).
     */
    private fun goForeground(state: LinkState) {
        val notification = RideNotification.build(this, state, stopIntent())
        val withLocation = locationAllowed()
        val connected = ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        val types = if (withLocation) connected or ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION else connected
        val located =
            try {
                ServiceCompat.startForeground(this, NOTIFICATION_ID, notification, types)
                withLocation
            } catch (e: SecurityException) {
                Log.w(TAG, "location refused to this start; connected-device only", e)
                ServiceCompat.startForeground(this, NOTIFICATION_ID, notification, connected)
                false
            }
        if (located) session?.startPosition()
        LinkRepository.update { it.copy(running = true, locationActive = session?.positionActive == true) }
    }

    private fun locationAllowed(): Boolean =
        checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED &&
            LocationManagerCompat.isLocationEnabled(getSystemService(LocationManager::class.java))

    private fun onLinkState(state: LinkState) {
        Log.i(TAG, "link: $state")
        LinkRepository.update { it.copy(link = state) }
        getSystemService(NotificationManager::class.java)
            .notify(NOTIFICATION_ID, RideNotification.build(this, state, stopIntent()))
    }

    private fun stopLink(): Int {
        session?.stop()
        session = null
        link?.stop()
        link = null
        LinkRepository.update { LinkRepository.Status() }
        ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
        stopSelf()
        return START_NOT_STICKY
    }

    private fun stopIntent(): Intent = Intent(this, RideLinkService::class.java).setAction(ACTION_STOP)

    override fun onDestroy() {
        session?.stop()
        link?.stop()
        LinkRepository.update { LinkRepository.Status() }
        super.onDestroy()
    }

    companion object {
        private const val TAG = "RideLink"
        private const val NOTIFICATION_ID = RideNotification.ID
        private const val ACTION_STOP = "io.github.d4rthc0d3r.sarathi.STOP"
        private const val ACTION_PRESENCE_GONE = "io.github.d4rthc0d3r.sarathi.PRESENCE_GONE"
        private const val EXTRA_FROM_PRESENCE = "from_presence"

        /** Starts (or keeps) the ride link. From the app, or from auto-start ([fromPresence]). */
        fun start(
            context: Context,
            fromPresence: Boolean = false,
        ) {
            val intent = Intent(context, RideLinkService::class.java).putExtra(EXTRA_FROM_PRESENCE, fromPresence)
            ContextCompat.startForegroundService(context, intent)
        }

        fun stop(context: Context) {
            context.startService(Intent(context, RideLinkService::class.java).setAction(ACTION_STOP))
        }

        /** The dash went away: a link auto-start started stops; one the rider started keeps waiting. */
        fun presenceGone(context: Context) {
            context.startService(Intent(context, RideLinkService::class.java).setAction(ACTION_PRESENCE_GONE))
        }
    }
}
