package io.github.d4rthc0d3r.sarathi.link

import android.content.Context
import androidx.core.content.edit
import io.github.d4rthc0d3r.sarathi.core.protocol.Lock
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * The moving lock's settings, kept on the phone and sent to the dash as `lock` (ADR-0011).
 * Defaults: moving at 5 km/h for 2 s, stopped below 3 km/h for 5 s, on.
 */
object LockSettings {
    private const val PREFS = "lock"
    private var state: MutableStateFlow<Lock>? = null

    fun flow(context: Context): StateFlow<Lock> = state ?: MutableStateFlow(load(context)).also { state = it }

    fun save(
        context: Context,
        lock: Lock,
    ) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit {
            putBoolean("enabled", lock.enabled)
            putInt("move_kmh", lock.moveKmh)
            putInt("move_s", lock.moveS)
            putInt("stop_kmh", lock.stopKmh)
            putInt("stop_s", lock.stopS)
        }
        (flow(context) as MutableStateFlow).value = lock
    }

    private fun load(context: Context): Lock {
        val p = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val default = Lock()
        return runCatching {
            Lock(
                p.getBoolean("enabled", default.enabled),
                p.getInt("move_kmh", default.moveKmh),
                p.getInt("move_s", default.moveS),
                p.getInt("stop_kmh", default.stopKmh),
                p.getInt("stop_s", default.stopS),
            )
        }.getOrDefault(default)
    }
}
