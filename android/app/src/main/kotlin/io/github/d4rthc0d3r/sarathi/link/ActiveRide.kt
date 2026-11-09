package io.github.d4rthc0d3r.sarathi.link

import io.github.d4rthc0d3r.sarathi.core.maps.GuidanceState
import io.github.d4rthc0d3r.sarathi.core.maps.Route
import io.github.d4rthc0d3r.sarathi.core.maps.RouteRequest
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend
import io.github.d4rthc0d3r.sarathi.core.maps.Skipped
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** What the rider confirmed (ADR-0008): the route, how it was asked for, and what was passed over. */
data class RidePlan(
    val route: Route,
    val request: RouteRequest,
    val skipped: List<Skipped<RoutingBackend>> = emptyList(),
    val destination: String,
)

/**
 * The ride in progress, for the whole app: the confirmation screen starts it, the ride service
 * ([RideNavigation]) follows it, Home shows it.
 */
object ActiveRide {
    private val _plan = MutableStateFlow<RidePlan?>(null)
    val plan: StateFlow<RidePlan?> = _plan.asStateFlow()

    private val _guidance = MutableStateFlow<GuidanceState?>(null)

    /** The latest guidance, for Home. */
    val guidance: StateFlow<GuidanceState?> = _guidance.asStateFlow()

    fun start(plan: RidePlan) {
        _guidance.value = null
        _plan.value = plan
    }

    /** The rider ended it, or it arrived. */
    fun end() {
        _plan.value = null
    }

    internal fun publish(state: GuidanceState?) {
        _guidance.value = state
    }
}
