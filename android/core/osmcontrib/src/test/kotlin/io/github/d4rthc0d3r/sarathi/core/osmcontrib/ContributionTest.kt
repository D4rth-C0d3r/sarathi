package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import io.github.d4rthc0d3r.sarathi.core.osmcontrib.TestMaps.at
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.TestMaps.ride
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ContributionTest {
    @Test
    fun `pkce matches RFC 7636's example`() {
        assertEquals(
            "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
            Pkce.challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
        )
        val v = Pkce.newSecret()
        assertEquals(43, v.length)
        assertTrue(v.matches(Regex("[A-Za-z0-9_-]+")))
        assertNotEquals(v, Pkce.newSecret())
    }

    @Test
    fun `gpx has every point with its time`() {
        val fixes = ride(listOf(at(0.0, 0.0), at(0.0, 30.0)))
        val gpx = Gpx.write(fixes, "sarathi <test> & co")
        assertEquals(fixes.size, Regex("<trkpt ").findAll(gpx).count())
        assertTrue(gpx.contains("<time>2023-11-14T22:13:20Z</time>"))
        assertTrue(gpx.contains("creator=\"sarathi &lt;test&gt; &amp; co\""))
    }

    // A 3 km ride east; a gap from 1,200 m to 1,600 m.
    private val track = ride(listOf(at(0.0, 0.0), at(0.0, 3000.0)))
    private val gap =
        track.filter { it.timeUtcMs - track[0].timeUtcMs in 120_000L..160_000L }.let { g ->
            Gap(
                g.map {
                    io.github.d4rthc0d3r.sarathi.core.mapdata
                        .GeoPoint(it.lat, it.lon)
                },
                400.0,
                g.first().timeUtcMs,
                g.last().timeUtcMs,
            )
        }

    @Test
    fun `the trace is the gap and a minute either side`() {
        val cut = TraceCut.forGap(track, gap, emptyList())
        assertEquals(gap.startUtcMs - TraceCut.CONTEXT_MS, cut.first().timeUtcMs)
        assertEquals(gap.endUtcMs + TraceCut.CONTEXT_MS, cut.last().timeUtcMs)
    }

    @Test
    fun `the privacy cut removes points near the start, the end and saved places`() {
        // A saved place on the route, just after the gap.
        val home = at(0.0, 1800.0)
        val cut = TraceCut.forGap(track, gap, listOf(home))
        assertTrue(
            cut.none {
                RoadIndex.segmentDistanceM(
                    io.github.d4rthc0d3r.sarathi.core.mapdata
                        .GeoPoint(it.lat, it.lon),
                    home,
                    home,
                ) <
                    300
            },
        )
        assertTrue(cut.isNotEmpty())
        // A short ride: everything is within 300 m of its start or end.
        val short = ride(listOf(at(0.0, 0.0), at(0.0, 500.0)))
        val g = Gap(listOf(at(0.0, 200.0), at(0.0, 300.0)), 100.0, short[20].timeUtcMs, short[30].timeUtcMs)
        assertTrue(TraceCut.forGap(short, g, emptyList()).isEmpty())
    }

    @Test
    fun `five of each a day`() {
        val day = 24L * 60 * 60 * 1000
        val limits = DailyLimits()
        repeat(DailyLimits.PER_DAY) { limits.record(Contribution.NOTE, 10 * day + it) }
        assertFalse(limits.allowed(Contribution.NOTE, 10 * day + 100))
        assertTrue(limits.allowed(Contribution.TRACE, 10 * day + 100))
        assertTrue(limits.allowed(Contribution.NOTE, 11 * day)) // a new day
        val back = DailyLimits.read(limits.lines().asSequence() + "bad;line")
        assertEquals(DailyLimits.PER_DAY, back.used(Contribution.NOTE, 10 * day + 5))
    }

    @Test
    fun `a sent gap keeps its OSM id`() {
        val stored = StoredGap("t.csv", gap, GapStatus.TRACE_SENT, "12345")
        val back = GapFile.read("t.csv", sequenceOf(GapFile.line(stored)))
        assertEquals("12345", back[0].ref)
        assertEquals(GapStatus.TRACE_SENT, back[0].status)
        // Not an id: dropped, the gap kept.
        val odd = GapFile.line(stored.copy(ref = null)) + ";<script>"
        assertEquals(null, GapFile.read("t.csv", sequenceOf(odd))[0].ref)
    }
}
