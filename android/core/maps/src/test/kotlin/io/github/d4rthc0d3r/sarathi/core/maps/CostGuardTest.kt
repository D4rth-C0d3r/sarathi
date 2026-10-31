package io.github.d4rthc0d3r.sarathi.core.maps

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.Instant
import java.time.ZoneId

class CostGuardTest {
    private var now = Instant.parse("2026-10-15T12:00:00Z")
    private val kolkata = ZoneId.of("Asia/Kolkata")

    private fun guard(saved: MutableList<Usage> = mutableListOf()) =
        CostGuard(CostGuard.caps(MapsSettings()), { now }, kolkata, save = { saved += it })

    @Test
    fun warnsAt80AndBlocksAt95Percent() {
        val g = guard()
        g.record(Meter.GOOGLE_NAV_DESTINATIONS, 799)
        assertEquals(GuardLevel.OK, g.status(Meter.GOOGLE_NAV_DESTINATIONS).level)
        g.record(Meter.GOOGLE_NAV_DESTINATIONS)
        assertEquals(GuardLevel.WARN, g.status(Meter.GOOGLE_NAV_DESTINATIONS).level)
        g.record(Meter.GOOGLE_NAV_DESTINATIONS, 150)
        assertTrue(g.blocked(Meter.GOOGLE_NAV_DESTINATIONS))
        assertEquals(950, g.status(Meter.GOOGLE_NAV_DESTINATIONS).used)
    }

    @Test
    fun googleMonthsTurnOverInPacificTime() {
        val g = guard()
        g.record(Meter.GOOGLE_AUTOCOMPLETE, 10)
        now = Instant.parse("2026-11-01T05:00:00Z") // still 31 Oct in California
        assertEquals(10, g.status(Meter.GOOGLE_AUTOCOMPLETE).used)
        now = Instant.parse("2026-11-01T08:00:00Z") // 1 Nov there
        assertEquals(0, g.status(Meter.GOOGLE_AUTOCOMPLETE).used)
    }

    @Test
    fun mapplsQuotaErrorBlocksForTheDay() {
        val g = guard()
        g.blockForPeriod(Meter.MAPPLS_ROUTING)
        assertTrue(g.blocked(Meter.MAPPLS_ROUTING))
        now = Instant.parse("2026-10-15T19:00:00Z") // 00:30 the next day in India
        assertFalse(g.blocked(Meter.MAPPLS_ROUTING))
    }

    @Test
    fun everyChangeIsSaved() {
        val saved = mutableListOf<Usage>()
        val g = guard(saved)
        g.record(Meter.MAPPLS_SEARCH, 3)
        assertEquals(3, saved.last().counts[Meter.MAPPLS_SEARCH]?.used)
        val reloaded = CostGuard(CostGuard.caps(MapsSettings()), { now }, kolkata, saved.last())
        assertEquals(3, reloaded.status(Meter.MAPPLS_SEARCH).used)
    }
}
