package io.github.d4rthc0d3r.sarathi.core.maps

import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class RideGuidanceTest {
    private val start = LatLng(15.49, 73.83)
    private val end = LatLng(15.55, 73.75)

    /** An engine whose off-route flag the test sets by hand. */
    private class FakeEngine(
        val route: Route,
        private val stops: List<LatLng>,
    ) : GuidanceEngine {
        override val state = MutableStateFlow<GuidanceState?>(null)
        var offRoute = false

        override fun start(route: Route) {
            state.value =
                GuidanceState(
                    StepManeuver.LEFT,
                    distanceToManeuverM = 100.0,
                    roadName = "",
                    remainingS = 60,
                    remainingM = 1_000.0,
                )
        }

        override fun update(fix: Fix) {
            state.value = state.value?.copy(offRoute = offRoute)
        }

        override fun stop() = Unit

        override fun remainingStops() = stops
    }

    private class Router(
        override val backend: RoutingBackend,
    ) : RoutingProvider {
        val requests = mutableListOf<RouteRequest>()

        override fun unavailable(): Unavailable? = null

        override suspend fun route(request: RouteRequest): Route {
            requests += request
            return Route(backend, listOf(request.origin) + request.stops, emptyList(), 1.0, 1, request.mode, "")
        }
    }

    private val valhalla = Router(RoutingBackend.OSM_VALHALLA)
    private val mappls = Router(RoutingBackend.MAPPLS)
    private val guard = CostGuard(CostGuard.caps(MapsSettings())) {}
    private val routing =
        RoutingService({ b -> listOf(valhalla, mappls).firstOrNull { it.backend == b } }, { MapsSettings() }, { guard })
    private val engines = mutableListOf<FakeEngine>()

    private fun fix(t: Long) = Fix(LatLng(15.50, 73.80), t)

    @Test
    fun aShortSpellOffTheRouteIsIgnoredALongerOneReroutesOnTheSameBackend() =
        runTest {
            val ride =
                RideGuidance(routing, { r, req -> FakeEngine(r, req.stops).also { engines += it } }, backgroundScope)
            val first =
                Route(RoutingBackend.OSM_VALHALLA, listOf(start, end), emptyList(), 1.0, 1, TravelMode.TWO_WHEELER, "")
            ride.start(first, RouteRequest(start, listOf(end)))
            runCurrent()

            engines[0].offRoute = true
            ride.update(fix(0))
            ride.update(fix(2_000))
            runCurrent()
            assertTrue(ride.state.value!!.offRoute)
            assertEquals(0, valhalla.requests.size) // 2 s off the route: not yet

            ride.update(fix(3_000))
            runCurrent()
            assertEquals(1, valhalla.requests.size) // same backend, though Mappls is first in the chain
            assertEquals(0, mappls.requests.size)
            assertEquals(LatLng(15.50, 73.80), valhalla.requests[0].origin)
            assertEquals(2, engines.size) // a new engine on the new route
            assertFalse(ride.state.value!!.rerouting)
        }

    @Test
    fun reroutesAreAtMostEvery10s() =
        runTest {
            val ride =
                RideGuidance(routing, { r, req ->
                    FakeEngine(r, req.stops).also {
                        it.offRoute = true
                        engines +=
                            it
                    }
                }, backgroundScope)
            ride.start(
                Route(RoutingBackend.OSM_VALHALLA, listOf(start, end), emptyList(), 1.0, 1, TravelMode.TWO_WHEELER, ""),
                RouteRequest(start, listOf(end)),
            )
            runCurrent()
            (0..9_000L step 1_000).forEach {
                ride.update(fix(it))
                runCurrent()
            }
            assertEquals(1, valhalla.requests.size)
            ride.update(fix(13_000))
            runCurrent()
            assertEquals(2, valhalla.requests.size)
            ride.stop()
            assertNull(ride.state.value)
        }
}
