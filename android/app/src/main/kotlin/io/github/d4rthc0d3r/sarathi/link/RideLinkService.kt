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
import io.github.d4rthc0d3r.sarathi.core.protocol.PhoneBattery
import io.github.d4rthc0d3r.sarathi.core.protocol.TimeSync
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter

/**
 * The ride link: a foreground service that keeps the dash connected with the screen off and the
 * phone in Doze (ADR-0010). Types connectedDevice and, when Android allows it, location. It
 * sends the time every 10 minutes and the phone's battery on change; navigation, calls and
 * music arrive in later steps.
 */
class RideLinkService : LifecycleService() {
    private var link: DashLink? = null
    private var startedByPresence = false
    private var locationCheck: LocationCheck? = null
    private var clock: Job? = null
    private var battery: PhoneBattery? = null
    private val batteryWatcher =
        BatteryWatcher(this) {
            battery = it
            send(it)
        }

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
        if (link == null) startedByPresence = fromPresence // a restart by Android (null intent) counts as the rider's
        goForeground(LinkState.Waiting)
        if (link == null) {
            val versionName = packageManager.getPackageInfo(packageName, 0).versionName ?: "0"
            link = DashLink(this, address, versionName, lifecycleScope).also { it.start() }
            batteryWatcher.start()
            lifecycleScope.launch { link?.state?.collect { onLinkState(it) } }
            lifecycleScope.launch { link?.received?.collect { Log.i(TAG, "from the dash: $it") } }
        }
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
        if (located && locationCheck == null) {
            locationCheck = LocationCheck(this).takeIf { it.start() }
        }
        LinkRepository.update { it.copy(running = true, locationActive = locationCheck != null) }
    }

    private fun locationAllowed(): Boolean =
        checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED &&
            LocationManagerCompat.isLocationEnabled(getSystemService(LocationManager::class.java))

    private fun onLinkState(state: LinkState) {
        LinkRepository.update { it.copy(link = state) }
        getSystemService(
            NotificationManager::class.java,
        ).notify(NOTIFICATION_ID, RideNotification.build(this, state, stopIntent()))
        clock?.cancel()
        if (state is LinkState.Ready) {
            clock = lifecycleScope.launch { sendTimeEvery10Minutes() }
            battery?.let { send(it) }
        }
    }

    private suspend fun sendTimeEvery10Minutes() {
        while (true) {
            val now = ZonedDateTime.now()
            send(TimeSync(now.format(LOCAL_TIME), now.offset.totalSeconds / SECONDS_PER_MINUTE))
            delay(TIME_PERIOD_MS)
        }
    }

    private fun send(message: io.github.d4rthc0d3r.sarathi.core.protocol.JsonMessage) {
        val dash = link ?: return
        if (dash.state.value !is LinkState.Ready) return
        lifecycleScope.launch {
            runCatching { dash.send(message) }.onFailure { Log.w(TAG, "send failed: ${it.message}") }
        }
    }

    private fun stopLink(): Int {
        link?.stop()
        link = null
        locationCheck?.stop()
        locationCheck = null
        batteryWatcher.stop()
        LinkRepository.update { LinkRepository.Status() }
        ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
        stopSelf()
        return START_NOT_STICKY
    }

    private fun stopIntent(): Intent = Intent(this, RideLinkService::class.java).setAction(ACTION_STOP)

    override fun onDestroy() {
        link?.stop()
        locationCheck?.stop()
        LinkRepository.update { LinkRepository.Status() }
        super.onDestroy()
    }

    companion object {
        private const val TAG = "RideLink"
        private const val NOTIFICATION_ID = RideNotification.ID
        private const val ACTION_STOP = "io.github.d4rthc0d3r.sarathi.STOP"
        private const val ACTION_PRESENCE_GONE = "io.github.d4rthc0d3r.sarathi.PRESENCE_GONE"
        private const val EXTRA_FROM_PRESENCE = "from_presence"
        private const val TIME_PERIOD_MS = 10 * 60 * 1000L
        private const val SECONDS_PER_MINUTE = 60
        private val LOCAL_TIME: DateTimeFormatter = DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH:mm:ss")

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
