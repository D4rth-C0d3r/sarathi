package io.github.d4rthc0d3r.sarathi.link

import android.content.ComponentName
import android.content.Context
import android.util.Log
import androidx.core.app.NotificationManagerCompat
import io.github.d4rthc0d3r.sarathi.core.ble.DashChannel
import io.github.d4rthc0d3r.sarathi.core.protocol.JsonMessage
import io.github.d4rthc0d3r.sarathi.core.protocol.PhoneCap
import io.github.d4rthc0d3r.sarathi.core.protocol.TimeSync
import io.github.d4rthc0d3r.sarathi.feature.calls.CallsFeature
import io.github.d4rthc0d3r.sarathi.feature.media.MediaFeature
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter

/**
 * Everything that runs on the link to the dash while the ride link is on: the time every 10
 * minutes, the phone's battery on change, and the features (music, then calls and position).
 * The service owns the link's lifetime; this owns what flows over it.
 */
class RideSession(
    private val context: Context,
    private val dash: DashChannel,
    private val scope: CoroutineScope,
) {
    private val jobs = mutableListOf<Job>()
    private var battery: io.github.d4rthc0d3r.sarathi.core.protocol.PhoneBattery? = null
    private val batteryWatcher =
        BatteryWatcher(context) {
            battery = it
            send(it)
        }
    private var media: MediaFeature? = null
    private var calls: CallsFeature? = null

    fun start() {
        batteryWatcher.start()
        media = MediaFeature(context, listenerComponent(context), dash, scope).takeIf { it.start() }
        calls = CallsFeature(context, dash, scope, CallNotifications.current).takeIf { it.start() }
        jobs += scope.launch { dash.received.collect { Log.i(TAG, "from the dash: $it") } }
        jobs +=
            scope.launch {
                var clock: Job? = null
                dash.ready.collect { ready ->
                    clock?.cancel()
                    if (ready) {
                        clock = launch { sendTimeEvery10Minutes() }
                        battery?.let { send(it) }
                    }
                }
            }
    }

    fun stop() {
        media?.stop()
        media = null
        calls?.stop()
        calls = null
        batteryWatcher.stop()
        jobs.forEach { it.cancel() }
        jobs.clear()
    }

    private suspend fun sendTimeEvery10Minutes() {
        while (true) {
            send(timeNow())
            delay(TIME_PERIOD_MS)
        }
    }

    private fun send(message: JsonMessage) {
        if (!dash.ready.value) return
        scope.launch {
            runCatching { dash.send(message) }
                .onSuccess { Log.d(TAG, "sent $message") }
                .onFailure { Log.w(TAG, "send failed: ${it.message}") }
        }
    }

    companion object {
        private const val TAG = "RideLink"
        private const val TIME_PERIOD_MS = 10 * 60 * 1000L
        private const val SECONDS_PER_MINUTE = 60
        private val LOCAL_TIME: DateTimeFormatter = DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH:mm:ss")

        fun timeNow(): TimeSync {
            val now = ZonedDateTime.now()
            return TimeSync(now.format(LOCAL_TIME), now.offset.totalSeconds / SECONDS_PER_MINUTE)
        }

        fun listenerComponent(context: Context) = ComponentName(context, DashNotificationListener::class.java)

        /** What this phone can do for the dash, in hello (protocol.md section 3). */
        fun caps(context: Context): Set<PhoneCap> =
            buildSet {
                if (NotificationManagerCompat.getEnabledListenerPackages(context).contains(context.packageName)) {
                    add(PhoneCap.MEDIA)
                }
                if (CallsFeature.allowed(context)) add(PhoneCap.CALLS)
            }
    }
}
