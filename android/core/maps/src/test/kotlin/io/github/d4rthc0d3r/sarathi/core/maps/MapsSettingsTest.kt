package io.github.d4rthc0d3r.sarathi.core.maps

import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend.GOOGLE
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend.MAPPLS
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend.OSM_GRAPHHOPPER
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend.OSM_OFFLINE
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend.OSM_VALHALLA
import kotlinx.serialization.json.Json
import org.junit.Assert.assertEquals
import org.junit.Test

class MapsSettingsTest {
    @Test
    fun defaultChainIsTheAdrs() {
        assertEquals(listOf(GOOGLE, MAPPLS, OSM_VALHALLA, OSM_GRAPHHOPPER, OSM_OFFLINE), MapsSettings().routingChain())
        assertEquals(
            listOf(SearchBackend.GOOGLE, SearchBackend.MAPPLS, SearchBackend.OSM),
            MapsSettings().searchChain(),
        )
    }

    @Test
    fun theChosenBackendGoesFirstAndOfflineStaysLast() {
        val mappls = MapsSettings(backend = SearchBackend.MAPPLS)
        assertEquals(listOf(MAPPLS, GOOGLE, OSM_VALHALLA, OSM_GRAPHHOPPER, OSM_OFFLINE), mappls.routingChain())
        assertEquals(SearchBackend.MAPPLS, mappls.search)
        val offline = MapsSettings(routingOverride = OSM_OFFLINE)
        assertEquals(OSM_OFFLINE, offline.routingChain().last())
        assertEquals(GOOGLE, offline.routingChain().first())
    }

    @Test
    fun overridesSplitSearchFromRouting() {
        val s =
            MapsSettings(backend = SearchBackend.GOOGLE, searchOverride = SearchBackend.OSM, routingOverride = MAPPLS)
        assertEquals(SearchBackend.OSM, s.search)
        assertEquals(MAPPLS, s.routing)
    }

    @Test
    fun reorderingTheFallback() {
        val s = MapsSettings().move(OSM_GRAPHHOPPER, -1)
        assertEquals(listOf(GOOGLE, MAPPLS, OSM_GRAPHHOPPER, OSM_VALHALLA), s.fallback)
        assertEquals(s, s.move(GOOGLE, -1)) // already first
    }

    @Test
    fun survivesAJsonRoundTrip() {
        val s = MapsSettings(backend = SearchBackend.OSM, routingOverride = OSM_VALHALLA, mapplsRoutesPerDay = 50)
        assertEquals(s, Json.decodeFromString<MapsSettings>(Json.encodeToString(s)))
    }
}
