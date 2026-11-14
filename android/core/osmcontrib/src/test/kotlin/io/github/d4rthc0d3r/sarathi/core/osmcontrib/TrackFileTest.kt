package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class TrackFileTest {
    private val a = RawGpsFix.fromProvider("gps", 15.491234, 73.827654, 1_700_000_000_000, 4.5f, 12.3f)!!
    private val b = RawGpsFix.fromProvider("gps", 15.491334, 73.827654, 1_700_000_001_000, 6f, null)!!

    @Test
    fun `writes and reads back`() {
        val text = sequenceOf(TrackFile.HEADER, TrackFile.line(a), TrackFile.line(b))
        val read = TrackFile.read(text)
        assertEquals(2, read.size)
        assertEquals(a.timeUtcMs, read[0].timeUtcMs)
        assertEquals(a.lat, read[0].lat, 1e-6)
        assertEquals(a.lon, read[0].lon, 1e-6)
        assertEquals(12.3f, read[0].speedMps!!, 0.01f)
        assertNull(read[1].speedMps)
    }

    @Test
    fun `a broken last line is skipped`() {
        val text = sequenceOf(TrackFile.HEADER, TrackFile.line(a), "1700000001000,15.49")
        assertEquals(1, TrackFile.read(text).size)
    }

    @Test
    fun `a file without our header reads as empty`() {
        assertTrue(TrackFile.read(sequenceOf(TrackFile.line(a))).isEmpty())
        assertTrue(TrackFile.read(emptySequence()).isEmpty())
    }

    @Test
    fun `names carry the start time`() {
        assertEquals(1_700_000_000_000, TrackFile.startOf(TrackFile.name(1_700_000_000_000)))
        assertNull(TrackFile.startOf("notes.txt"))
        assertNull(TrackFile.startOf("track-x.csv"))
    }

    @Test
    fun `tracks expire after 30 days`() {
        val day = 24L * 60 * 60 * 1000
        val now = 100 * day
        val names = listOf(TrackFile.name(now - 31 * day), TrackFile.name(now - 29 * day), "other.txt")
        assertEquals(listOf(TrackFile.name(now - 31 * day)), TrackFile.expired(names, now))
    }

    @Test
    fun `length sums the fixes`() {
        assertEquals(11.1, TrackFile.lengthM(listOf(a, b)), 0.2)
    }
}
