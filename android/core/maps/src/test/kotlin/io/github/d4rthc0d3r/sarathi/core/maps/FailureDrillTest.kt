package io.github.d4rthc0d3r.sarathi.core.maps

import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class FailureDrillTest {
    private class Router(
        override val backend: RoutingBackend,
    ) : RoutingProvider {
        var calls = 0

        override fun unavailable(): Unavailable? = null

        override suspend fun route(request: RouteRequest): Route {
            calls++
            return Route(backend, listOf(request.origin) + request.stops, emptyList(), 1.0, 1, request.mode, "")
        }
    }

    private val routers = RoutingBackend.entries.map { Router(it) }
    private val drill = FailureDrill()

    /** The rider's own order starts with Mappls; the drill walks its own order regardless. */
    private val settings = MapsSettings(backend = SearchBackend.MAPPLS)
    private val service =
        RoutingService({ b ->
            routers.first {
                it.backend == b
            }
        }, { settings }, { CostGuard(CostGuard.caps(MapsSettings())) {} }, drill)
    private val request = RouteRequest(LatLng(15.49, 73.83), listOf(LatLng(15.55, 73.75)))

    @Test
    fun eachRequestFailsOneMoreStageWithoutCallingTheFailedBackends() =
        runTest {
            drill.start()
            val first = service.route(request)
            assertEquals(RoutingBackend.MAPPLS, first.backend)
            assertEquals(Unavailable.OVER_QUOTA, first.skipped.single().reason)
            assertTrue(drill.reroutesDue)

            val second = service.route(request, prefer = RoutingBackend.MAPPLS)
            assertEquals(RoutingBackend.OSM_VALHALLA, second.backend)

            val third = service.route(request, prefer = RoutingBackend.OSM_VALHALLA)
            assertEquals(RoutingBackend.OSM_OFFLINE, third.backend)
            assertFalse(drill.reroutesDue)

            // Failed backends were never called: no cost.
            assertEquals(0, routers.first { it.backend == RoutingBackend.GOOGLE }.calls)
            assertEquals(1, routers.first { it.backend == RoutingBackend.MAPPLS }.calls)
            assertEquals(0, routers.first { it.backend == RoutingBackend.OSM_GRAPHHOPPER }.calls)

            drill.stop()
            assertEquals(RoutingBackend.MAPPLS, service.route(request).backend) // the settings' order again
        }
}
