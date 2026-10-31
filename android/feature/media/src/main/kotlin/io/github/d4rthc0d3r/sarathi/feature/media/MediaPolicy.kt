package io.github.d4rthc0d3r.sarathi.feature.media

import io.github.d4rthc0d3r.sarathi.core.protocol.DashText
import io.github.d4rthc0d3r.sarathi.core.protocol.Media

/** One media session as the policy sees it (no Android types, so it unit-tests on the JVM). */
data class SessionInfo(
    val playing: Boolean,
    val title: String?,
    val artist: String?,
    val positionMs: Long,
    val durationMs: Long,
)

/** Which session the dash follows, and what to send (protocol.md section 3, `media`). */
object MediaPolicy {
    private const val MS_PER_S = 1000L
    private const val MAX_S = 86_400L

    /** The playing session if any, else the most recent; Android lists them most recent first. */
    fun pick(sessions: List<SessionInfo>): SessionInfo? = sessions.firstOrNull { it.playing } ?: sessions.firstOrNull()

    fun message(session: SessionInfo?): Media {
        if (session == null) return Media(false, false, "", "", 0, 0)
        return Media(
            avail = true,
            playing = session.playing,
            title = DashText.of(session.title.orEmpty(), Media.TEXT_BYTES),
            artist = DashText.of(session.artist.orEmpty(), Media.TEXT_BYTES),
            posS = seconds(session.positionMs),
            durS = seconds(session.durationMs),
        )
    }

    /**
     * Whether [now] needs sending after [sent]: anything but the position changed, or the
     * position jumped (a seek); the steady 5 s refresh while playing is separate.
     */
    fun changed(
        sent: Media?,
        now: Media,
    ): Boolean = sent == null || sent.copy(posS = now.posS) != now || kotlin.math.abs(now.posS - sent.posS) > SEEK_S

    private fun seconds(ms: Long) = (ms / MS_PER_S).coerceIn(0, MAX_S).toInt()

    /** A position change bigger than this between refreshes is a seek, sent at once. */
    private const val SEEK_S = 7
}
