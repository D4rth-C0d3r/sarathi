package io.github.d4rthc0d3r.sarathi.maps.osm

import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.maps.RouteRequest
import io.github.d4rthc0d3r.sarathi.core.maps.StepManeuver
import io.github.d4rthc0d3r.sarathi.core.maps.TravelMode
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs

/** Panaji KTC Bus Stand → Calangute Beach, recorded from both services on 2026-11-09. */
class RoutingParsersTest {
    private fun fixture(name: String) = javaClass.getResource("/$name")!!.readText()

    private val panaji = LatLng(15.4953203, 73.8370972)
    private val calangute = LatLng(15.549441, 73.7534857)

    private fun near(
        a: LatLng,
        b: LatLng,
    ) = abs(a.lat - b.lat) < 0.002 && abs(a.lng - b.lng) < 0.002

    @Test
    fun valhalla() {
        val r = ValhallaRouting.parse(fixture("valhalla-panaji-calangute.json"), TravelMode.TWO_WHEELER)
        assertEquals(13_893.0, r.distanceM, 1.0)
        assertTrue(near(panaji, r.points.first()) && near(calangute, r.points.last()))
        assertEquals(StepManeuver.DEPART, r.steps.first().maneuver)
        assertEquals(StepManeuver.ARRIVE, r.steps.last().maneuver)
        val roundabout = r.steps.first { it.maneuver == StepManeuver.ROUNDABOUT }
        assertEquals(3, roundabout.roundaboutExit)
        // Roundabout exits fold into the roundabout step, so steps add up to the whole route.
        assertEquals(r.distanceM, r.steps.sumOf { it.distanceM }, 1.0)
        assertTrue(r.steps.zipWithNext().all { (a, b) -> a.pointIndex <= b.pointIndex })
        assertTrue(r.steps.last().pointIndex <= r.points.lastIndex)
    }

    @Test
    fun graphHopper() {
        val r = GraphHopperRouting.parse(fixture("graphhopper-panaji-calangute.json"))
        assertEquals(13_858.5, r.distanceM, 1.0)
        assertEquals(TravelMode.DRIVING, r.mode) // the free plan's car profile
        assertTrue(near(panaji, r.points.first()) && near(calangute, r.points.last()))
        assertEquals(StepManeuver.DEPART, r.steps.first().maneuver)
        assertEquals(StepManeuver.ARRIVE, r.steps.last().maneuver)
        assertEquals(3, r.steps.first { it.maneuver == StepManeuver.ROUNDABOUT }.roundaboutExit)
    }

    @Test
    fun valhallaRequestUsesMotorcycleCosting() {
        val body = ValhallaRouting.body(RouteRequest(panaji, listOf(calangute)))
        assertEquals("\"motorcycle\"", body["costing"].toString())
    }
}
