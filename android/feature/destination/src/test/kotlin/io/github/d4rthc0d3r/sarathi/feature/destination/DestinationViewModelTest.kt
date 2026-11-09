package io.github.d4rthc0d3r.sarathi.feature.destination

import io.github.d4rthc0d3r.sarathi.core.maps.CostGuard
import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.maps.MapsSettings
import io.github.d4rthc0d3r.sarathi.core.maps.Place
import io.github.d4rthc0d3r.sarathi.core.maps.PlaceSource
import io.github.d4rthc0d3r.sarathi.core.maps.PlacesBook
import io.github.d4rthc0d3r.sarathi.core.maps.SearchBackend
import io.github.d4rthc0d3r.sarathi.core.maps.SearchProvider
import io.github.d4rthc0d3r.sarathi.core.maps.SearchService
import io.github.d4rthc0d3r.sarathi.core.maps.SearchSession
import io.github.d4rthc0d3r.sarathi.core.maps.Suggestion
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class DestinationViewModelTest {
    private val dispatcher = StandardTestDispatcher()
    private val basilica = LatLng(15.5007, 73.9117)

    private class FakeGoogle(
        val at: LatLng,
    ) : SearchProvider {
        override val backend = SearchBackend.GOOGLE
        val queries = mutableListOf<String>()
        val sessions = mutableListOf<String>()
        var details = 0

        override fun unavailable(): Unavailable? = null

        override suspend fun autocomplete(
            query: String,
            near: LatLng?,
            session: SearchSession,
        ): List<Suggestion> {
            queries += query
            sessions += session.token
            return listOf(Suggestion("Basilica of Bom Jesus", "Old Goa", backend, "ChIJb"))
        }

        override suspend fun details(
            suggestion: Suggestion,
            session: SearchSession,
        ): Place {
            details++
            sessions += session.token
            return Place(suggestion.title, "Old Goa, Goa", at, PlaceSource.GOOGLE, suggestion.id)
        }
    }

    private class FakeStore(
        initial: PlacesBook = PlacesBook(),
    ) : PlacesStore {
        override val book = MutableStateFlow(initial)

        override fun update(change: (PlacesBook) -> PlacesBook) {
            book.value = change(book.value)
        }
    }

    private val google = FakeGoogle(basilica)
    private val guard = CostGuard(CostGuard.caps(MapsSettings())) {}

    private val search = SearchService({ b -> google.takeIf { it.backend == b } }, { MapsSettings() }, { guard })

    /** The recorded redirect of the fixture "google-place" (docs/fixtures/share-links.json). */
    private val redirects =
        mapOf(
            "https://maps.app.goo.gl/VHHR9qEiTznWY9Cu7?g_st=ac" to
                "https://www.google.com/maps/place/Basilica+of+Bom+Jesus,+Old+Goa+Road,+Old+Goa,+Goa+403402/" +
                "data=!4m2!3m1!1s0x3bbfbef2b99b0289:0x1e8056689f15a7b8!18m1!1e1",
        )

    private fun model(store: PlacesStore = FakeStore()) =
        DestinationViewModel(search, store, SharedLinkResolver(search, { null }, { redirects[it] }), { null })

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun typingPausesThenSearchesOnceAndTheChoiceBecomesRecent() =
        runTest(dispatcher) {
            val store = FakeStore()
            val vm = model(store)
            vm.onQuery("b")
            vm.onQuery("bo")
            vm.onQuery("bom")
            advanceTimeBy(DestinationViewModel.PAUSE_MS - 1)
            assertEquals(emptyList<String>(), google.queries)
            advanceUntilIdle()
            assertEquals(listOf("bom"), google.queries)
            assertEquals(SearchBackend.GOOGLE, vm.ui.value.backend)

            vm.onSuggestion(
                vm.ui.value.suggestions
                    .single(),
            )
            advanceUntilIdle()
            assertEquals(
                basilica,
                vm.ui.value.chosen
                    ?.at,
            )
            assertEquals(1, google.sessions.distinct().size) // one session, closed by details
            assertEquals(
                "ChIJb",
                store.book.value.recents
                    .single()
                    .place.id,
            )
        }

    @Test
    fun anExpiredGooglePlaceIsLookedUpAgain() =
        runTest(dispatcher) {
            val expired = Place("Basilica of Bom Jesus", "Old Goa", null, PlaceSource.GOOGLE, "ChIJb")
            val store = FakeStore(PlacesBook().addRecent(expired, 0))
            val vm = model(store)
            vm.onKept(expired)
            advanceUntilIdle()
            assertEquals(1, google.details)
            assertEquals(
                basilica,
                store.book.value.recents
                    .single()
                    .place.at,
            )
        }

    @Test
    fun aSharedGooglePlaceIsFollowedAndLookedUpByName() =
        runTest(dispatcher) {
            val vm = model()
            vm.onShared("https://maps.app.goo.gl/VHHR9qEiTznWY9Cu7?g_st=ac")
            advanceUntilIdle()
            assertEquals(listOf("Basilica of Bom Jesus, Old Goa Road, Old Goa, Goa 403402"), google.queries)
            assertEquals(
                basilica,
                vm.ui.value.chosen
                    ?.at,
            )
            assertEquals(
                PlaceSource.GOOGLE,
                vm.ui.value.chosen
                    ?.source,
            )
        }

    @Test
    fun sharedCoordinatesNeedNoLookupAndUnknownTextSaysSo() =
        runTest(dispatcher) {
            val vm = model()
            vm.onShared("Fort Aguada\nhttps://www.openstreetmap.org/?mlat=15.491618&mlon=73.774014")
            advanceUntilIdle()
            assertEquals(
                LatLng(15.491618, 73.774014),
                vm.ui.value.chosen
                    ?.at,
            )
            assertEquals(
                PlaceSource.SHARED,
                vm.ui.value.chosen
                    ?.source,
            )
            assertEquals(0, google.details)

            vm.onShared("see you there")
            advanceUntilIdle()
            assertEquals(
                LinkProblem.NOT_SUPPORTED,
                vm.ui.value.linkProblem
                    ?.problem,
            )
        }
}
