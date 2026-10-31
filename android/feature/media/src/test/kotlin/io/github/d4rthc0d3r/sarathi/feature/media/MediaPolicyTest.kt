package io.github.d4rthc0d3r.sarathi.feature.media

import io.github.d4rthc0d3r.sarathi.core.protocol.Media
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class MediaPolicyTest {
    private val paused = SessionInfo(false, "Podcast", "Host", 10_000, 3_600_000)
    private val playing = SessionInfo(true, "Kesariya", "Arijit Singh", 25_400, 268_000)

    @Test
    fun followsThePlayingSessionElseTheMostRecent() {
        assertEquals(playing, MediaPolicy.pick(listOf(paused, playing)))
        assertEquals(paused, MediaPolicy.pick(listOf(paused, paused.copy(title = "Older"))))
        assertEquals(null, MediaPolicy.pick(emptyList()))
    }

    @Test
    fun messageForTheDash() {
        assertEquals(Media(true, true, "Kesariya", "Arijit Singh", 25, 268), MediaPolicy.message(playing))
        assertEquals(Media(false, false, "", "", 0, 0), MediaPolicy.message(null))
        assertEquals("Deja vu", MediaPolicy.message(playing.copy(title = "Déjà vu")).title)
    }

    @Test
    fun sendsChangesAndSeeksNotTicks() {
        val sent = MediaPolicy.message(playing)
        assertFalse(MediaPolicy.changed(sent, sent.copy(posS = sent.posS + 3))) // the 5 s refresh covers it
        assertTrue(MediaPolicy.changed(sent, sent.copy(posS = sent.posS + 60))) // a seek
        assertTrue(MediaPolicy.changed(sent, sent.copy(playing = false)))
        assertTrue(MediaPolicy.changed(sent, sent.copy(title = "Next")))
        assertTrue(MediaPolicy.changed(null, sent))
    }
}
