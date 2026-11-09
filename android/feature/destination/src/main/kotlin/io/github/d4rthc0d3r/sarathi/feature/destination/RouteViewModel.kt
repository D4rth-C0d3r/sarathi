package io.github.d4rthc0d3r.sarathi.feature.destination

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import io.github.d4rthc0d3r.sarathi.core.maps.ChainResult
import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.maps.Place
import io.github.d4rthc0d3r.sarathi.core.maps.Route
import io.github.d4rthc0d3r.sarathi.core.maps.RouteRequest
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingService
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

data class RouteUi(
    val routing: Boolean = false,
    /** Which backend routed it and which were skipped (ADR-0006). */
    val result: ChainResult<RoutingBackend, Route>? = null,
    /** No current position to start from. */
    val noPosition: Boolean = false,
)

/**
 * A route to the chosen destination or shared stops, for checking backends at the desk. The
 * confirmation screen (step F7) grows from this.
 */
class RouteViewModel(
    private val routing: RoutingService,
    private val near: () -> LatLng?,
) : ViewModel() {
    private val _ui = MutableStateFlow(RouteUi())
    val ui: StateFlow<RouteUi> = _ui.asStateFlow()
    private var job: Job? = null

    /** From [origin], or the current position when it's null, through [stops] in order. */
    fun route(
        origin: Place?,
        stops: List<Place>,
    ) {
        val from = origin?.at ?: near()
        val to = stops.mapNotNull { it.at }
        if (from == null || to.size != stops.size || to.isEmpty()) {
            _ui.value = RouteUi(noPosition = from == null)
            return
        }
        job?.cancel()
        _ui.value = RouteUi(routing = true)
        job = viewModelScope.launch { _ui.value = RouteUi(result = routing.route(RouteRequest(from, to))) }
    }

    fun clear() {
        job?.cancel()
        _ui.value = RouteUi()
    }
}
