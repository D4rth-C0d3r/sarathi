package io.github.d4rthc0d3r.sarathi.core.maps

import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Test

class OsrmJsonTest {
    private val points = listOf(LatLng(15.49, 73.83), LatLng(15.50, 73.83), LatLng(15.50, 73.84), LatLng(15.51, 73.84))
    private val route =
        Route(
            RoutingBackend.OSM_VALHALLA,
            points,
            listOf(
                RouteStep(StepManeuver.DEPART, 0, "", 1_100.0),
                RouteStep(StepManeuver.ROUNDABOUT, 1, "NH 66", 1_070.0, roundaboutExit = 2),
                RouteStep(StepManeuver.LEFT, 2, "Calangute Road", 1_100.0),
                RouteStep(StepManeuver.ARRIVE, 3, "", 0.0),
            ),
            3_270.0,
            600,
            TravelMode.TWO_WHEELER,
            "test",
        )

    @Test
    fun oneStepPerOurStepWithItsStretchOfTheLine() {
        val json = OsrmJson.route(route)
        val steps =
            json["legs"]!!
                .jsonArray[0]
                .jsonObject["steps"]!!
                .jsonArray
                .map { it.jsonObject }
        assertEquals(4, steps.size)
        val lines = steps.map { Polyline.decode(it["geometry"]!!.jsonPrimitive.content, 6) }
        assertEquals(listOf(points[0], points[1]), lines[0])
        assertEquals(listOf(points[3], points[3]), lines[3]) // the arrival: one point, twice
        assertEquals(points, Polyline.decode(json["geometry"]!!.jsonPrimitive.content, 6))
        val roundabout = steps[1]["maneuver"]!!.jsonObject
        assertEquals("roundabout", roundabout["type"]!!.jsonPrimitive.content)
        assertEquals(2, roundabout["exit"]!!.jsonPrimitive.int)
        // Durations follow distance and add up.
        assertEquals(600.0, steps.sumOf { it["duration"]!!.jsonPrimitive.content.toDouble() }, 0.01)
    }

    @Test
    fun maneuversRoundTrip() {
        StepManeuver.entries.filter { it != StepManeuver.FERRY }.forEach { m ->
            val (type, modifier) = OsrmJson.osrm(m, lastStep = true)
            assertEquals(m.name, m, Osrm.maneuver(type, modifier, firstLeg = true))
        }
    }

    @Test
    fun polylineEncodesWhatItDecodes() {
        assertEquals(
            "_p~iF~ps|U_ulLnnqC_mqNvxq`@",
            Polyline.encode(listOf(LatLng(38.5, -120.2), LatLng(40.7, -120.95), LatLng(43.252, -126.453)), 5),
        )
    }
}
