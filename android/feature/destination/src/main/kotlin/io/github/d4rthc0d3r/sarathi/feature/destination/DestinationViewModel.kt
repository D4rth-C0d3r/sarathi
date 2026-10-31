package io.github.d4rthc0d3r.sarathi.feature.destination

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import io.github.d4rthc0d3r.sarathi.core.maps.Label
import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.maps.Place
import io.github.d4rthc0d3r.sarathi.core.maps.PlaceSource
import io.github.d4rthc0d3r.sarathi.core.maps.PlacesBook
import io.github.d4rthc0d3r.sarathi.core.maps.ProviderException
import io.github.d4rthc0d3r.sarathi.core.maps.SavedPlace
import io.github.d4rthc0d3r.sarathi.core.maps.SearchBackend
import io.github.d4rthc0d3r.sarathi.core.maps.SearchService
import io.github.d4rthc0d3r.sarathi.core.maps.SearchSession
import io.github.d4rthc0d3r.sarathi.core.maps.Skipped
import io.github.d4rthc0d3r.sarathi.core.maps.Suggestion
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable
import kotlinx.coroutines.FlowPreview
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.debounce
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import java.util.UUID

data class DestinationUi(
    val query: String = "",
    val suggestions: List<Suggestion> = emptyList(),
    val searching: Boolean = false,
    /** Who answered the last search, and who was skipped on the way (ADR-0006). */
    val backend: SearchBackend? = null,
    val skipped: List<Skipped<SearchBackend>> = emptyList(),
    val failure: Unavailable? = null,
    val book: PlacesBook = PlacesBook(),
    val chosen: Place? = null,
    val resolving: Boolean = false,
)

/**
 * Search, recents and saved places (ADR-0008). Typing pauses [PAUSE_MS] before a request, which
 * also keeps Photon to one request per 300 ms; one session runs from the first keystroke to the
 * chosen place.
 */
private fun newSession() = SearchSession(UUID.randomUUID().toString())

@OptIn(FlowPreview::class)
class DestinationViewModel(
    private val search: SearchService,
    private val store: PlacesStore,
    private val near: () -> LatLng?,
    private val clock: () -> Long = System::currentTimeMillis,
) : ViewModel() {
    private val _ui = MutableStateFlow(DestinationUi(book = store.book.value))
    val ui: StateFlow<DestinationUi> = _ui.asStateFlow()

    private val queries = MutableStateFlow("")
    private var session = newSession()

    init {
        viewModelScope.launch { store.book.collect { book -> _ui.update { it.copy(book = book) } } }
        viewModelScope.launch { queries.debounce(PAUSE_MS).collectLatest(::runSearch) }
    }

    fun onQuery(text: String) {
        _ui.update { it.copy(query = text, chosen = null, failure = null) }
        queries.value = text.trim()
    }

    fun onSuggestion(suggestion: Suggestion) = resolve { search.details(suggestion, session) }

    /** A recent or saved place; one whose coordinates expired (Google, 30 days) is looked up again. */
    fun onKept(place: Place) {
        if (!PlacesBook.needsLookup(place)) {
            choose(place)
            return
        }
        val backend = backendOf(place.source) ?: return
        resolve { search.details(Suggestion(place.name, place.address, backend, place.id.orEmpty()), session) }
    }

    fun save(
        label: Label,
        title: String,
    ) {
        val place = _ui.value.chosen ?: return
        store.update { it.save(label, title, place, clock()) }
    }

    fun removeRecent(place: Place) = store.update { it.removeRecent(place) }

    fun removeSaved(entry: SavedPlace) = store.update { it.remove(entry) }

    fun clearChosen() = _ui.update { it.copy(chosen = null) }

    private suspend fun runSearch(query: String) {
        if (query.length < MIN_QUERY) {
            _ui.update { it.copy(suggestions = emptyList(), searching = false, backend = null, skipped = emptyList()) }
            return
        }
        _ui.update { it.copy(searching = true) }
        val result = search.autocomplete(query, near(), session)
        _ui.update {
            it.copy(
                suggestions = result.value.orEmpty(),
                searching = false,
                backend = result.backend,
                skipped = result.skipped,
                failure =
                    if (result.backend ==
                        null
                    ) {
                        result.skipped.lastOrNull()?.reason ?: Unavailable.ERROR
                    } else {
                        null
                    },
            )
        }
    }

    private fun resolve(lookup: suspend () -> Place) {
        _ui.update { it.copy(resolving = true, failure = null) }
        viewModelScope.launch {
            try {
                choose(lookup())
            } catch (e: ProviderException) {
                _ui.update { it.copy(failure = e.reason) }
            } finally {
                _ui.update { it.copy(resolving = false) }
                session = newSession()
            }
        }
    }

    private fun choose(place: Place) {
        val now = clock()
        store.update { it.refreshed(place, now).addRecent(place, now) }
        _ui.update { it.copy(chosen = place) }
    }

    companion object {
        const val PAUSE_MS = 300L
        const val MIN_QUERY = 2

        fun backendOf(source: PlaceSource): SearchBackend? =
            when (source) {
                PlaceSource.GOOGLE -> SearchBackend.GOOGLE
                PlaceSource.MAPPLS -> SearchBackend.MAPPLS
                PlaceSource.OSM -> SearchBackend.OSM
                PlaceSource.SHARED, PlaceSource.PICKED -> null
            }
    }
}
