package io.github.d4rthc0d3r.sarathi.core.protocol

import org.junit.Assert.assertEquals
import org.junit.Test

class DashTextTest {
    @Test
    fun accentsAndSymbols() {
        assertEquals("Beyonce - Deja vu", DashText.of("Beyoncé – Déjà vu", 48))
        assertEquals("(c) OpenStreetMap", DashText.of("© OpenStreetMap", 48))
        assertEquals("25° • 3 km", DashText.of("25° • 3 km", 48))
        assertEquals("Don't Stop - Live...", DashText.of("Don’t Stop — Live…", 48))
    }

    @Test
    fun scriptsTheDashCantDrawAreDropped() {
        assertEquals("", DashText.of("गांधी मार्ग", 48))
        assertEquals("Arijit Singh", DashText.of("Arijit Singh अरिजीत", 48))
    }

    @Test
    fun cutToTheByteLimit() {
        assertEquals("abc", DashText.of("abcdef", 3))
        assertEquals("Kesariya", DashText.of("Kesariya (From \"Brahmastra\")", 9))
        assertEquals(48, DashText.of("x".repeat(100), Media.TEXT_BYTES).length)
        assertEquals("a b", DashText.of("  a \n\t b ", 48))
    }
}
