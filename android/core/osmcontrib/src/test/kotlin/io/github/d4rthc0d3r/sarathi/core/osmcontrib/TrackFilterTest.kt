package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class TrackFilterTest {
    // About 11.1 m per 0.0001 degree of latitude.
    private fun fix(
        s: Int,
        north: Int,
        speed: Float? = 10f,
        accuracy: Float = 5f,
    ) = RawGpsFix.fromProvider("gps", 15.5 + north * 0.0001, 73.8, 1_700_000_000_000 + s * 1000L, accuracy, speed)!!

    @Test
    fun `keeps accurate moving fixes`() {
        val f = TrackFilter()
        assertTrue(f.accept(fix(0, 0)))
        assertTrue(f.accept(fix(1, 1)))
    }

    @Test
    fun `drops inaccurate fixes`() {
        assertFalse(TrackFilter().accept(fix(0, 0, accuracy = 16f)))
    }

    @Test
    fun `drops standing still, by reported or implied speed`() {
        val f = TrackFilter()
        assertFalse(f.accept(fix(0, 0, speed = 0.5f)))
        assertFalse(f.accept(fix(1, 0, speed = null))) // no speed, no movement since the last fix
        assertTrue(f.accept(fix(2, 2, speed = null))) // 22 m in a second: moving
    }

    @Test
    fun `drops jumps and fixes out of order`() {
        val f = TrackFilter()
        assertTrue(f.accept(fix(0, 0)))
        assertFalse(f.accept(fix(1, 100))) // 1.1 km in a second
        assertFalse(f.accept(fix(0, 1))) // not after the last fix
    }

    @Test
    fun `the first fix without speed is not kept`() {
        assertFalse(TrackFilter().accept(fix(0, 0, speed = null)))
    }
}
