package io.github.d4rthc0d3r.sarathi.link

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import androidx.core.content.ContextCompat
import io.github.d4rthc0d3r.sarathi.core.protocol.PhoneBattery

/** Reports the phone's battery (level and charging) whenever either changes, for `phone_batt`. */
class BatteryWatcher(
    private val context: Context,
    private val onChange: (PhoneBattery) -> Unit,
) {
    private var last: PhoneBattery? = null
    private val receiver =
        object : BroadcastReceiver() {
            override fun onReceive(
                context: Context,
                intent: Intent,
            ) = onBattery(intent)
        }

    fun start() {
        ContextCompat.registerReceiver(
            context,
            receiver,
            IntentFilter(Intent.ACTION_BATTERY_CHANGED),
            ContextCompat.RECEIVER_NOT_EXPORTED,
        )
    }

    fun stop() {
        runCatching { context.unregisterReceiver(receiver) }
        last = null
    }

    private fun onBattery(intent: Intent) {
        val level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
        val scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, -1)
        if (level < 0 || scale <= 0) return
        val status = intent.getIntExtra(BatteryManager.EXTRA_STATUS, -1)
        val charging = status == BatteryManager.BATTERY_STATUS_CHARGING || status == BatteryManager.BATTERY_STATUS_FULL
        val now = PhoneBattery(level * PERCENT / scale, charging)
        if (now != last) {
            last = now
            onChange(now)
        }
    }

    private companion object {
        const val PERCENT = 100
    }
}
