package io.github.d4rthc0d3r.sarathi.core.maps

import io.github.d4rthc0d3r.sarathi.core.maps.TrafficLevel.FREE
import io.github.d4rthc0d3r.sarathi.core.maps.TrafficLevel.JAM
import io.github.d4rthc0d3r.sarathi.core.maps.TrafficLevel.SLOW
import io.github.d4rthc0d3r.sarathi.core.maps.TrafficLevel.UNKNOWN
import org.junit.Assert.assertEquals
import org.junit.Test

class TrafficTest {
    @Test
    fun mapplsSegmentsMergeIntoStretches() {
        val levels = listOf("low", "low", "moderate", "heavy", "heavy", "unknown").map(Traffic::mapplsLevel)
        val s = Traffic.fromSegments(listOf(100.0, 50.0, 20.0, 30.0, 30.0, 10.0), levels)
        assertEquals(
            listOf(
                TrafficStretch(0.0, 150.0, FREE),
                TrafficStretch(150.0, 20.0, SLOW),
                TrafficStretch(170.0, 60.0, JAM),
                TrafficStretch(230.0, 10.0, UNKNOWN),
            ),
            s,
        )
    }

    @Test
    fun googleLegsBecomeOneRouteWithFreeGaps() {
        val legs =
            listOf(
                listOf(TrafficStretch(200.0, 100.0, SLOW)),
                listOf(TrafficStretch(0.0, 50.0, JAM), TrafficStretch(400.0, 500.0, SLOW)),
            )
        val s = Traffic.fromLegs(listOf(1_000.0, 600.0), legs)
        assertEquals(
            listOf(
                TrafficStretch(0.0, 200.0, FREE),
                TrafficStretch(200.0, 100.0, SLOW),
                TrafficStretch(300.0, 700.0, FREE),
                TrafficStretch(1_000.0, 50.0, JAM),
                TrafficStretch(1_050.0, 350.0, FREE),
                // Clipped to the leg's end.
                TrafficStretch(1_400.0, 200.0, SLOW),
            ),
            s,
        )
        assertEquals(1_600.0, s.sumOf { it.lengthM }, 0.0)
    }

    @Test
    fun totalsAndDistance() {
        val t =
            Traffic.totals(
                listOf(
                    TrafficStretch(0.0, 10.0, FREE),
                    TrafficStretch(10.0, 5.0, JAM),
                    TrafficStretch(15.0, 5.0, FREE),
                ),
            )
        assertEquals(mapOf(FREE to 15.0, JAM to 5.0), t)
        // Panaji KTC to Calangute Beach in a straight line: about 10.8 km.
        assertEquals(10_770.0, Geo.distanceM(LatLng(15.4953203, 73.8370972), LatLng(15.549441, 73.7534857)), 100.0)
    }
}
