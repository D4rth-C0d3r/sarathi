package io.github.d4rthc0d3r.sarathi.maps.google

import android.app.Service
import android.content.Intent
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.Messenger
import com.google.android.libraries.mapsplatform.turnbyturn.TurnByTurnManager
import com.google.android.libraries.mapsplatform.turnbyturn.model.NavInfo
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Google's turn-by-turn data feed (ADR-0006): the Navigation SDK binds this service during
 * guidance and sends it the trip's [NavInfo] about once a second. Not exported.
 */
class GoogleNavUpdatesService : Service() {
    private val manager = TurnByTurnManager.createInstance()
    private val messenger =
        Messenger(
            Handler(Looper.getMainLooper()) { msg ->
                if (msg.what ==
                    TurnByTurnManager.MSG_NAV_INFO
                ) {
                    GoogleNavFeed.publish(manager.readNavInfoFromBundle(msg.data))
                }
                true
            },
        )

    override fun onBind(intent: Intent?): IBinder = messenger.binder
}

/** The feed's latest [NavInfo], for [GoogleGuidance]. */
object GoogleNavFeed {
    private val latest = MutableStateFlow<NavInfo?>(null)
    val navInfo: StateFlow<NavInfo?> = latest.asStateFlow()

    internal fun publish(info: NavInfo) {
        latest.value = info
    }

    internal fun clear() {
        latest.value = null
    }
}
