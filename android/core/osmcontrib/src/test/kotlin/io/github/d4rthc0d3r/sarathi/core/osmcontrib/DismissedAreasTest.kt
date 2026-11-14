package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import io.github.d4rthc0d3r.sarathi.core.osmcontrib.TestMaps.at
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class DismissedAreasTest {
    private fun gap(east: Double) = Gap((0..30).map { at(100.0, east + it * 10.0) }, 300.0, 1, 2)

    @Test
    fun `a dismissed place is never offered again, others are`() {
        val d = DismissedAreas()
        d.dismiss(gap(0.0))
        assertTrue(d.covers(gap(20.0))) // the same place, ridden a little differently
        assertFalse(d.covers(gap(2_000.0)))
    }

    @Test
    fun `survives being written and read`() {
        val d = DismissedAreas().apply { dismiss(gap(0.0)) }
        val back = DismissedAreas.read(d.lines().asSequence() + "garbage")
        assertEquals(1, back.areas.size)
        assertTrue(back.covers(gap(0.0)))
    }
}
