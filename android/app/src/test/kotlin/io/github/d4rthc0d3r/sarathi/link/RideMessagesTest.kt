package io.github.d4rthc0d3r.sarathi.link

import io.github.d4rthc0d3r.sarathi.core.maps.GuardLevel
import io.github.d4rthc0d3r.sarathi.core.maps.GuidanceState
import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.maps.Meter
import io.github.d4rthc0d3r.sarathi.core.maps.MeterStatus
import io.github.d4rthc0d3r.sarathi.core.maps.Route
import io.github.d4rthc0d3r.sarathi.core.maps.RouteStep
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend
import io.github.d4rthc0d3r.sarathi.core.maps.SearchBackend
import io.github.d4rthc0d3r.sarathi.core.maps.Skipped
import io.github.d4rthc0d3r.sarathi.core.maps.StepManeuver
import io.github.d4rthc0d3r.sarathi.core.maps.TravelMode
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable
import io.github.d4rthc0d3r.sarathi.core.protocol.BinaryCodec
import io.github.d4rthc0d3r.sarathi.core.protocol.EventKind
import io.github.d4rthc0d3r.sarathi.core.protocol.FallbackReason
import io.github.d4rthc0d3r.sarathi.core.protocol.Maneuver
import io.github.d4rthc0d3r.sarathi.core.protocol.NavFlag
import io.github.d4rthc0d3r.sarathi.core.protocol.RideFrame
import io.github.d4rthc0d3r.sarathi.core.protocol.Routing
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random
import io.github.d4rthc0d3r.sarathi.core.protocol.Route as DashRoute

class RideMessagesTest {
    private val frame = RideFrame.near(15.49, 73.83, Random(7))

    /** A winding line of [n] points, about 5 m apart, with a turn every 1,000 points. */
    private fun winding(n: Int): Route {
        val points = (0 until n).map { i -> LatLng(15.49 + i * 0.00004, 73.83 + Math.sin(i / 3.0) * 0.0002) }
        val steps =
            listOf(RouteStep(StepManeuver.DEPART, 0, "", 0.0)) +
                (1000 until n step 1000).map { RouteStep(StepManeuver.LEFT, it, "NH 66", 0.0) } +
                RouteStep(StepManeuver.ARRIVE, n - 1, "", 0.0)
        return Route(
            RoutingBackend.OSM_VALHALLA,
            points,
            steps,
            5.0 * n,
            600,
            TravelMode.TWO_WHEELER,
            "© OpenStreetMap contributors",
        )
    }

    @Test
    fun aLongRouteIsThinnedButKeepsItsTurnsExactly() {
        val route = winding(12_000)
        val msg = RideMessages.route(route, frame, routeId = 42, version = 1)
        assertTrue(msg.points.size in 2..DashRoute.MAX_POINTS)
        assertEquals(route.steps.size, msg.markers.size)
        route.steps.zip(msg.markers).forEach { (step, marker) ->
            val at = route.points[step.pointIndex].let { frame.project(it.lat, it.lng) }
            val p = msg.points[marker.point]
            assertEquals(Math.round(at.x), p.x.toLong())
            assertEquals(Math.round(at.y), p.y.toLong())
        }
        assertEquals(Maneuver.LEFT.code, msg.markers[1].maneuver)
        assertEquals(frame.epoch, msg.epoch)
        assertEquals(Routing.OSM_ONLINE, msg.backend)
        // Valid for the protocol: encodes and decodes back.
        assertEquals(msg, BinaryCodec.decode(msg.type, BinaryCodec.encode(msg)))
    }

    @Test
    fun aShortRouteKeepsItsShape() {
        val msg = RideMessages.route(winding(200), frame, routeId = 1, version = 0)
        assertTrue(msg.points.size > 50) // a 1 m tolerance keeps the bends of a 5 m zigzag
    }

    @Test
    fun navCarriesTheGuidanceWithinTheProtocolsLimits() {
        val g =
            GuidanceState(
                StepManeuver.ROUNDABOUT,
                3,
                351.6,
                "Café Road–NH 66",
                remainingS = 1_000_000,
                remainingM = 12_345.4,
                offRoute = true,
                rerouting = true,
            )
        val nav = RideMessages.nav(g)
        assertEquals("roundabout", nav.m)
        assertEquals(3, nav.exit)
        assertEquals(352L, nav.distM)
        assertEquals("Cafe Road-NH 66", nav.street) // ASCII for the dash
        assertEquals(604_800L, nav.remS) // clamped to a week
        assertEquals(setOf(NavFlag.OFF_ROUTE, NavFlag.REROUTING), nav.flags)
    }

    @Test
    fun fallbacksAndCostWarnings() {
        val skipped =
            listOf(
                Skipped(RoutingBackend.GOOGLE, Unavailable.OVER_QUOTA),
                Skipped(RoutingBackend.MAPPLS, Unavailable.NO_KEY), // a setting, not a fallback
                Skipped(RoutingBackend.OSM_VALHALLA, Unavailable.TIMEOUT),
            )
        val events = RideMessages.fallbacks(skipped, RoutingBackend.OSM_GRAPHHOPPER)
        assertEquals(listOf(FallbackReason.QUOTA, FallbackReason.OFFLINE), events.map { it.reason })
        assertTrue(events.all { it.kind == EventKind.FALLBACK && it.to == Routing.OSM_ONLINE })

        assertNull(
            RideMessages.costWarning(
                MeterStatus(Meter.GOOGLE_NAV_DESTINATIONS, 799, 1000, GuardLevel.OK),
                SearchBackend.GOOGLE,
            ),
        )
        assertEquals(
            80,
            RideMessages
                .costWarning(
                    MeterStatus(Meter.GOOGLE_NAV_DESTINATIONS, 800, 1000, GuardLevel.WARN),
                    SearchBackend.GOOGLE,
                )?.percent,
        )
    }

    @Test
    fun backendTextIsForTheDash() {
        val b = RideMessages.backend(winding(10), SearchBackend.OSM, "OSM online")
        assertEquals("(c) OpenStreetMap contributors", b.attribution)
        assertTrue(b.twoWheeler)
    }
}
