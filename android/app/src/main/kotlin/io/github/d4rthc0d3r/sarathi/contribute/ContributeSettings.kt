package io.github.d4rthc0d3r.sarathi.contribute

import android.content.Context
import androidx.core.content.edit
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * The OSM contribution helper's switch (ADR-0012): off by default. Turning it off deletes the
 * recorded tracks, their gaps and the dismissed places.
 */
object ContributeSettings {
    private const val PREFS = "osm_contribute"
    private const val ENABLED = "enabled"
    private var state: MutableStateFlow<Boolean>? = null

    fun flow(context: Context): StateFlow<Boolean> =
        state ?: MutableStateFlow(
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(ENABLED, false),
        ).also { state = it }

    fun set(
        context: Context,
        enabled: Boolean,
    ) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit { putBoolean(ENABLED, enabled) }
        (flow(context) as MutableStateFlow).value = enabled
        if (!enabled) {
            TrackStorage.deleteAll(context)
            GapStore.deleteAll(context)
        }
    }
}
