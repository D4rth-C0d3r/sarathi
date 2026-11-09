package io.github.d4rthc0d3r.sarathi.core.maps

import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Test

class RoutingServiceTest {
    private class Fake(
        override val backend: RoutingBackend,
        var fail: Unavailable? = null,
    ) : RoutingProvider {
        var calls = 0

        override fun unavailable(): Unavailable? = null

        override suspend fun route(request: RouteRequest): Route {
            calls++
            fail?.let { throw ProviderException(it) }
            return Route(
                backend,
                listOf(request.origin) + request.stops,
                emptyList(),
                1.0,
                1,
                request.mode,
                backend.name,
            )
        }
    }

    private val google = Fake(RoutingBackend.GOOGLE)
    private val valhalla = Fake(RoutingBackend.OSM_VALHALLA)
    private val guard = CostGuard(CostGuard.caps(MapsSettings())) {}
    private val service =
        RoutingService({ b -> listOf(google, valhalla).firstOrNull { it.backend == b } }, { MapsSettings() }, { guard })
    private val goa = LatLng(15.49, 73.83)
    private val threeStops = RouteRequest(goa, listOf(LatLng(15.58, 73.81), LatLng(15.50, 73.91), LatLng(15.55, 73.75)))

    @Test
    fun googleCountsEveryStopAsADestination() =
        runTest {
            assertEquals(RoutingBackend.GOOGLE, service.route(threeStops).backend)
            assertEquals(3, guard.status(Meter.GOOGLE_NAV_DESTINATIONS).used)
        }

    @Test
    fun aRequestThatWouldPassTheCapGoesToTheNextBackend() =
        runTest {
            val cap = guard.status(Meter.GOOGLE_NAV_DESTINATIONS).cap
            guard.record(Meter.GOOGLE_NAV_DESTINATIONS, cap - 2)
            val result = service.route(threeStops)
            assertEquals(RoutingBackend.OSM_VALHALLA, result.backend)
            assertEquals(0, google.calls)
            assertEquals(Unavailable.OVER_QUOTA, result.skipped.first().reason)
        }

    @Test
    fun aQuotaErrorFromGoogleBlocksItForTheMonth() =
        runTest {
            google.fail = Unavailable.OVER_QUOTA
            assertEquals(RoutingBackend.OSM_VALHALLA, service.route(threeStops).backend)
            assertEquals(GuardLevel.BLOCKED, guard.status(Meter.GOOGLE_NAV_DESTINATIONS).level)
        }
}
