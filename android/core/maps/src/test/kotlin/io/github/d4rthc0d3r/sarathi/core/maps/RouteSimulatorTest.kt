package io.github.d4rthc0d3r.sarathi.core.maps

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class RouteSimulatorTest {
    // About 1.1 km north, then about 1.07 km east, near Panaji.
    private val line = listOf(LatLng(15.49, 73.83), LatLng(15.50, 73.83), LatLng(15.50, 73.84))

    @Test
    fun walksTheLineAtTheSpeed() {
        val sim = RouteSimulator(line, speedMps = 10.0)
        val fixes = sim.fixes().toList()
        assertEquals(line.first(), fixes.first().at)
        assertEquals(line.last().lat, fixes.last().at.lat, 1e-9)
        assertEquals(line.last().lng, fixes.last().at.lng, 1e-9)
        // One fix a second at 10 m/s: about one per 10 m, each 10 m from the one before.
        assertEquals(Math.ceil(sim.lengthM / 10.0).toInt() + 1, fixes.size)
        assertEquals(10.0, Geo.distanceM(fixes[3].at, fixes[4].at), 0.05)
        assertEquals(0.0, fixes[5].bearingDeg!!, 0.5) // north first
        assertEquals(90.0, fixes[fixes.size - 3].bearingDeg!!, 0.5) // then east
        assertEquals(1_000L, fixes[1].timeMs)
    }

    @Test
    fun aDetourLeavesTheLineForAStretch() {
        val sim =
            RouteSimulator(
                line,
                speedMps = 10.0,
                detour = RouteSimulator.Detour(atM = 300.0, lengthM = 200.0, offsetM = 80.0),
            )
        val fixes = sim.fixes().toList()
        // Heading north, "right" is east: the detour fixes sit 80 m east of the line.
        val off = fixes.filter { it.at.lng > 73.83 + 1e-6 && it.at.lat < 15.5 - 1e-6 }
        assertEquals(20, off.size)
        assertTrue(off.all { abs(Geo.distanceM(it.at, LatLng(it.at.lat, 73.83)) - 80.0) < 0.5 })
    }

    @Test
    fun bearingAndOffsetAgree() {
        val p = LatLng(15.49, 73.83)
        val q = Geo.offset(p, 45.0, 1_000.0)
        assertEquals(1_000.0, Geo.distanceM(p, q), 0.5)
        assertEquals(45.0, Geo.bearingDeg(p, q), 0.1)
    }

    private fun abs(x: Double) = kotlin.math.abs(x)
}
