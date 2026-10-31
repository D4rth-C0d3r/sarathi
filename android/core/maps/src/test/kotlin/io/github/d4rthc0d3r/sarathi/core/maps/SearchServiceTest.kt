package io.github.d4rthc0d3r.sarathi.core.maps

import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class SearchServiceTest {
    private class Fake(
        override val backend: SearchBackend,
        var fail: Unavailable? = null,
        val at: LatLng? = null,
    ) : SearchProvider {
        var autocompletes = 0
        var detailsCalls = 0

        override fun unavailable(): Unavailable? = null

        override suspend fun autocomplete(
            query: String,
            near: LatLng?,
            session: SearchSession,
        ): List<Suggestion> {
            autocompletes++
            fail?.let { throw ProviderException(it) }
            return listOf(Suggestion(query, "", backend, "id-$query", at))
        }

        override suspend fun details(
            suggestion: Suggestion,
            session: SearchSession,
        ): Place {
            detailsCalls++
            return Place(suggestion.title, "", LatLng(15.5, 73.9), SearchService.sourceOf(backend), suggestion.id)
        }
    }

    private val google = Fake(SearchBackend.GOOGLE)
    private val mappls = Fake(SearchBackend.MAPPLS)
    private val osm = Fake(SearchBackend.OSM, at = LatLng(15.5, 73.8))
    private var settings = MapsSettings()
    private val guard = CostGuard(CostGuard.caps(settings)) {}
    private val service =
        SearchService({ b -> listOf(google, mappls, osm).first { it.backend == b } }, { settings }, { guard })
    private val session = SearchSession("s")

    @Test
    fun googleIsCountedAndItsSuggestionsAlwaysNeedDetails() =
        runTest {
            val result = service.autocomplete("Bom Jesus", null, session)
            assertEquals(SearchBackend.GOOGLE, result.backend)
            val place = service.details(result.value!!.single().copy(at = LatLng(1.0, 1.0)), session)
            assertEquals(1, google.detailsCalls)
            assertEquals(PlaceSource.GOOGLE, place.source)
            assertEquals(1, guard.status(Meter.GOOGLE_AUTOCOMPLETE).used)
            assertEquals(1, guard.status(Meter.GOOGLE_PLACE_DETAILS).used)
        }

    @Test
    fun quotaErrorBlocksTheMeterAndFallsBack() =
        runTest {
            settings = MapsSettings(backend = SearchBackend.MAPPLS)
            mappls.fail = Unavailable.OVER_QUOTA
            val result = service.autocomplete("Panaji", null, session)
            assertEquals(SearchBackend.GOOGLE, result.backend)
            assertTrue(guard.blocked(Meter.MAPPLS_SEARCH))
            // Blocked now: the next search doesn't call Mappls at all.
            service.autocomplete("Panaji", null, session)
            assertEquals(1, mappls.autocompletes)
        }

    @Test
    fun photonSuggestionsNeedNoDetailsCall() =
        runTest {
            settings = MapsSettings(backend = SearchBackend.OSM)
            val suggestion = service.autocomplete("Fort Aguada", null, session).value!!.single()
            val place = service.details(suggestion, session)
            assertEquals(0, osm.detailsCalls)
            assertEquals(LatLng(15.5, 73.8), place.at)
            assertNull(guard.status(Meter.GOOGLE_AUTOCOMPLETE).used.takeIf { it > 0 })
        }
}
