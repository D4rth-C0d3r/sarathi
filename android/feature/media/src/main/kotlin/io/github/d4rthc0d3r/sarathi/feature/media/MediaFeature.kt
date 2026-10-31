package io.github.d4rthc0d3r.sarathi.feature.media

import android.content.ComponentName
import android.content.Context
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import io.github.d4rthc0d3r.sarathi.core.ble.DashChannel
import io.github.d4rthc0d3r.sarathi.core.protocol.Media
import io.github.d4rthc0d3r.sarathi.core.protocol.MediaCmd
import io.github.d4rthc0d3r.sarathi.core.protocol.MediaKey
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * Music on the dash: follows the phone's active media session (needs notification access,
 * granted to [listener]), sends `media` on change and every 5 s while playing, and carries out
 * the dash's `media_cmd`. Everything runs on the main thread.
 */
class MediaFeature(
    context: Context,
    private val listener: ComponentName,
    private val dash: DashChannel,
    private val scope: CoroutineScope,
) {
    private val sessions = context.getSystemService(MediaSessionManager::class.java)
    private val main = Handler(Looper.getMainLooper())
    private var controller: MediaController? = null
    private var sent: Media? = null
    private val jobs = mutableListOf<Job>()

    private val sessionsChanged =
        MediaSessionManager.OnActiveSessionsChangedListener { follow(it.orEmpty()) }

    private val callback =
        object : MediaController.Callback() {
            override fun onMetadataChanged(metadata: MediaMetadata?) = publish()

            override fun onPlaybackStateChanged(state: PlaybackState?) = publish()

            override fun onSessionDestroyed() = follow(currentSessions())
        }

    /** Starts following; false without notification access (the app asks for it in setup). */
    fun start(): Boolean {
        val active =
            try {
                sessions.addOnActiveSessionsChangedListener(sessionsChanged, listener, main)
                currentSessions()
            } catch (e: SecurityException) {
                Log.w(TAG, "no notification access: music stays off", e)
                return false
            }
        follow(active)
        jobs += scope.launch { dash.received.collect { if (it is MediaCmd) command(it.cmd) } }
        jobs += scope.launch { dash.ready.collect { if (it) send(force = true) } }
        jobs += scope.launch { refreshWhilePlaying() }
        return true
    }

    fun stop() {
        runCatching { sessions.removeOnActiveSessionsChangedListener(sessionsChanged) }
        controller?.unregisterCallback(callback)
        controller = null
        jobs.forEach { it.cancel() }
        jobs.clear()
    }

    private fun currentSessions(): List<MediaController> = sessions.getActiveSessions(listener)

    private fun follow(active: List<MediaController>) {
        val infos = active.map { it.info() }
        val chosen = MediaPolicy.pick(infos)?.let { active[infos.indexOf(it)] }
        if (chosen?.sessionToken != controller?.sessionToken) {
            controller?.unregisterCallback(callback)
            controller = chosen
            chosen?.registerCallback(callback, main)
        }
        publish()
    }

    private fun publish() = send(force = false)

    private fun send(force: Boolean) {
        val now = MediaPolicy.message(controller?.info())
        if (!force && !MediaPolicy.changed(sent, now)) return
        sent = now
        if (!dash.ready.value) return
        scope.launch { runCatching { dash.send(now) }.onFailure { Log.w(TAG, "media not sent: ${it.message}") } }
    }

    private suspend fun refreshWhilePlaying() {
        while (true) {
            delay(REFRESH_MS)
            if (controller?.info()?.playing == true) send(force = true)
        }
    }

    private fun command(key: MediaKey) {
        val c = controller ?: return
        when (key) {
            MediaKey.PLAY_PAUSE -> {
                if (c.playbackState?.state ==
                    PlaybackState.STATE_PLAYING
                ) {
                    c.transportControls.pause()
                } else {
                    c.transportControls.play()
                }
            }

            MediaKey.NEXT -> {
                c.transportControls.skipToNext()
            }

            MediaKey.PREV -> {
                c.transportControls.skipToPrevious()
            }
        }
    }

    private fun MediaController.info(): SessionInfo {
        val state = playbackState
        val playing = state?.state == PlaybackState.STATE_PLAYING
        // The position advances by itself while playing; the state says when it was last set.
        val position =
            when {
                state == null -> {
                    0L
                }

                playing && state.lastPositionUpdateTime > 0 -> {
                    state.position +
                        ((SystemClock.elapsedRealtime() - state.lastPositionUpdateTime) * state.playbackSpeed).toLong()
                }

                else -> {
                    state.position
                }
            }
        return SessionInfo(
            playing = playing,
            title = metadata?.getString(MediaMetadata.METADATA_KEY_TITLE),
            artist = metadata?.getString(MediaMetadata.METADATA_KEY_ARTIST),
            positionMs = position,
            durationMs = metadata?.getLong(MediaMetadata.METADATA_KEY_DURATION) ?: 0,
        )
    }

    private companion object {
        const val TAG = "Media"
        const val REFRESH_MS = 5_000L
    }
}
