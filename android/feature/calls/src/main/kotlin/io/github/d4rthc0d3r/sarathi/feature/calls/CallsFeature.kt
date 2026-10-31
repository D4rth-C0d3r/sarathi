package io.github.d4rthc0d3r.sarathi.feature.calls

import android.Manifest
import android.annotation.SuppressLint
import android.app.ActivityOptions
import android.app.PendingIntent
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.telecom.TelecomManager
import android.telephony.PhoneStateListener
import android.telephony.TelephonyCallback
import android.telephony.TelephonyManager
import android.util.Log
import io.github.d4rthc0d3r.sarathi.core.ble.DashChannel
import io.github.d4rthc0d3r.sarathi.core.protocol.Call
import io.github.d4rthc0d3r.sarathi.core.protocol.CallAction
import io.github.d4rthc0d3r.sarathi.core.protocol.CallChoice
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/**
 * Calls on the dash: the phase and the caller's name, and the dash's accept and reject. Cellular
 * calls go through TelecomManager (ANSWER_PHONE_CALLS); internet calls through their call
 * notification's buttons. [notices] is the latest call notification, from :app's listener.
 */
class CallsFeature(
    private val context: Context,
    private val dash: DashChannel,
    private val scope: CoroutineScope,
    private val notices: StateFlow<CallNotice?>,
) {
    private val telephonyManager = context.getSystemService(TelephonyManager::class.java)
    private val telecom = context.getSystemService(TelecomManager::class.java)
    private var telephony = Telephony.IDLE
    private var sent: Call? = null
    private val jobs = mutableListOf<Job>()
    private var unregister: () -> Unit = {}

    /** Starts; false without the Phone permissions (the app asks for them in setup). */
    fun start(): Boolean {
        if (!allowed(context)) return false
        unregister = listenToCallState()
        jobs += scope.launch { notices.collect { publish() } }
        jobs += scope.launch { dash.received.collect { if (it is CallAction) act(it.action) } }
        jobs += scope.launch { dash.ready.collect { if (it) publish(force = true) } }
        return true
    }

    fun stop() {
        unregister()
        jobs.forEach { it.cancel() }
        jobs.clear()
    }

    private fun onCallState(state: Int) {
        telephony =
            when (state) {
                TelephonyManager.CALL_STATE_RINGING -> Telephony.RINGING
                TelephonyManager.CALL_STATE_OFFHOOK -> Telephony.OFFHOOK
                else -> Telephony.IDLE
            }
        publish()
    }

    private fun publish(force: Boolean = false) {
        val now = CallPolicy.message(telephony, notices.value?.info)
        if (!force && now == sent) return
        sent = now
        if (!dash.ready.value) return
        scope.launch { runCatching { dash.send(now) }.onFailure { Log.w(TAG, "call not sent: ${it.message}") } }
    }

    @SuppressLint("MissingPermission") // start() checked ANSWER_PHONE_CALLS
    private fun act(choice: CallChoice) {
        val notice = notices.value
        if (CallPolicy.isInternetCall(telephony, notice?.info)) {
            val button = if (choice == CallChoice.ACCEPT) notice?.answer else notice?.decline
            button?.let { press(it) } ?: Log.w(TAG, "no $choice button on this call")
            return
        }
        // Deprecated since Android 10 in favour of InCallService, which only the default dialer
        // gets; still the documented way for an app holding ANSWER_PHONE_CALLS.
        @Suppress("DEPRECATION")
        when (choice) {
            CallChoice.ACCEPT -> {
                if (telephony == Telephony.RINGING) telecom.acceptRingingCall()
            }

            CallChoice.REJECT -> {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                    telecom.endCall()
                } else {
                    Log.w(TAG, "ending a call needs Android 9")
                }
            }
        }
    }

    /** Presses a notification button; companion-device apps may start activities from the background. */
    private fun press(intent: PendingIntent) {
        val options =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                ActivityOptions
                    .makeBasic()
                    .setPendingIntentBackgroundActivityStartMode(ActivityOptions.MODE_BACKGROUND_ACTIVITY_START_ALLOWED)
                    .toBundle()
            } else {
                null
            }
        runCatching {
            intent.send(context, 0, null, null, null, null, options)
        }.onFailure { Log.w(TAG, "call button failed: ${it.message}") }
    }

    @SuppressLint("MissingPermission") // start() checked READ_PHONE_STATE
    private fun listenToCallState(): () -> Unit {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val callback =
                object : TelephonyCallback(), TelephonyCallback.CallStateListener {
                    override fun onCallStateChanged(state: Int) = onCallState(state)
                }
            telephonyManager.registerTelephonyCallback(context.mainExecutor, callback)
            return { telephonyManager.unregisterTelephonyCallback(callback) }
        }
        @Suppress("DEPRECATION") // the only way before Android 12
        val listener =
            object : PhoneStateListener() {
                @Deprecated("Android 11 and below")
                override fun onCallStateChanged(
                    state: Int,
                    phoneNumber: String?,
                ) = onCallState(state)
            }
        @Suppress("DEPRECATION")
        telephonyManager.listen(listener, PhoneStateListener.LISTEN_CALL_STATE)
        @Suppress("DEPRECATION")
        return { telephonyManager.listen(listener, PhoneStateListener.LISTEN_NONE) }
    }

    companion object {
        private const val TAG = "Calls"

        /** Both Phone permissions (one prompt): accept/reject, and the call state. */
        val PERMISSIONS = arrayOf(Manifest.permission.ANSWER_PHONE_CALLS, Manifest.permission.READ_PHONE_STATE)

        fun allowed(context: Context): Boolean =
            PERMISSIONS.all { context.checkSelfPermission(it) == PackageManager.PERMISSION_GRANTED }
    }
}
