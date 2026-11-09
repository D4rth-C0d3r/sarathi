package io.github.d4rthc0d3r.sarathi.core.maps

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class OsrmTest {
    /** Encodes points at precision 6, the inverse of [Polyline.decode], for test lines. */
    private fun encode(vararg points: Pair<Double, Double>): String {
        val out = StringBuilder()
        var lastLat = 0L
        var lastLng = 0L

        fun put(v: Long) {
            var x = if (v < 0) (v shl 1).inv() else v shl 1
            while (x >= 0x20) {
                out.append(((0x20 or (x and 0x1f).toInt()) + 63).toChar())
                x = x shr 5
            }
            out.append((x + 63).toInt().toChar())
        }
        points.forEach { (lat, lng) ->
            val la = Math.round(lat * 1e6)
            val ln = Math.round(lng * 1e6)
            put(la - lastLat)
            put(ln - lastLng)
            lastLat = la
            lastLng = ln
        }
        return out.toString()
    }

    @Test
    fun stepsJoinIntoOneLineAndRoundaboutExitsFold() {
        val a = 15.49 to 73.83
        val b = 15.50 to 73.82
        val c = 15.51 to 73.81
        val d = 15.52 to 73.80
        val legs =
            listOf(
                listOf(
                    OsrmStep("depart", null, null, "", 100.0, encode(a, b)),
                    OsrmStep("roundabout", "right", 3, "NH 66", 50.0, encode(b, c)),
                    OsrmStep("exit roundabout", "right", 3, "NH 66", 20.0, encode(c, c)),
                    OsrmStep("turn", "sharp left", null, "Calangute Road", 300.0, encode(c, d)),
                    OsrmStep("arrive", null, null, "", 0.0, encode(d, d)),
                ),
            )
        val r = Osrm.route(RoutingBackend.MAPPLS, legs, 470.0, 600.0, TravelMode.TWO_WHEELER, "Mappls")
        assertEquals(4, r.points.size)
        assertEquals(
            listOf(StepManeuver.DEPART, StepManeuver.ROUNDABOUT, StepManeuver.SHARP_LEFT, StepManeuver.ARRIVE),
            r.steps.map { it.maneuver },
        )
        assertEquals(listOf(0, 1, 2, 3), r.steps.map { it.pointIndex })
        assertEquals(70.0, r.steps[1].distanceM, 0.0) // the exit step folded into the roundabout
        assertEquals(3, r.steps[1].roundaboutExit)
        assertEquals("Calangute Road", r.via)
    }

    @Test
    fun maneuvers() {
        assertEquals(StepManeuver.KEEP_RIGHT, Osrm.maneuver("continue", "slight right", true))
        assertEquals(StepManeuver.FORK_LEFT, Osrm.maneuver("fork", "slight left", true))
        assertEquals(StepManeuver.RAMP_RIGHT, Osrm.maneuver("off ramp", "right", true))
        assertEquals(StepManeuver.UTURN, Osrm.maneuver("end of road", "uturn", true))
        assertNull(Osrm.maneuver("depart", null, firstLeg = false))
    }
}
