package io.github.d4rthc0d3r.sarathi.core.maps

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Guidance for a whole ride (ADR-0006): one [GuidanceEngine] per route, and reroutes for engines
 * that don't reroute themselves. Off the route for [OFF_ROUTE_MS] of fixes, it asks for a route
 * from the current position to the remaining stops, on the same backend first and then the
 * fallback chain, at most every [MIN_REROUTE_MS]. While that runs the state says "rerouting";
 * it never freezes on the old route silently. Time comes from the fixes, so a simulated ride can
 * run faster than real time.
 */
class RideGuidance(
    private val routing: RoutingService,
    private val engineFor: (Route, RouteRequest) -> GuidanceEngine,
    private val scope: CoroutineScope,
    private val log: (String) -> Unit = {},
) {
    private val _state = MutableStateFlow<GuidanceState?>(null)
    val state: StateFlow<GuidanceState?> = _state.asStateFlow()

    private val _route = MutableStateFlow<Route?>(null)

    /** The route being followed; a reroute replaces it (a new route version for the dash). */
    val route: StateFlow<Route?> = _route.asStateFlow()

    private var engine: GuidanceEngine? = null
    private var request: RouteRequest? = null
    private var stateJob: Job? = null
    private var rerouteJob: Job? = null
    private var offRouteSinceMs: Long? = null
    private var lastRerouteMs = Long.MIN_VALUE / 2
    private var rerouting = false

    fun start(
        route: Route,
        request: RouteRequest,
    ) {
        engine?.stop()
        stateJob?.cancel()
        this.request = request
        offRouteSinceMs = null
        val e = engineFor(route, request).also { it.start(route) }
        engine = e
        _route.value = route
        stateJob =
            scope.launch { e.state.collect { s -> _state.value = s?.copy(rerouting = rerouting || s.rerouting) } }
    }

    fun update(fix: Fix) {
        val e = engine ?: return
        e.update(fix)
        val s = e.state.value
        val off = s != null && s.offRoute && !s.arrived && !e.reroutesItself
        if (!off) {
            offRouteSinceMs = null
        } else {
            val since = offRouteSinceMs ?: fix.timeMs.also { offRouteSinceMs = it }
            val due = fix.timeMs - since >= OFF_ROUTE_MS && fix.timeMs - lastRerouteMs >= MIN_REROUTE_MS
            if (due && !rerouting) reroute(fix, e)
        }
    }

    fun stop() {
        rerouteJob?.cancel()
        stateJob?.cancel()
        engine?.stop()
        engine = null
        _state.value = null
        _route.value = null
    }

    private fun reroute(
        fix: Fix,
        e: GuidanceEngine,
    ) {
        val old = _route.value ?: return
        val mode = request?.mode ?: TravelMode.TWO_WHEELER
        val stops = e.remainingStops().ifEmpty { return }
        lastRerouteMs = fix.timeMs
        setRerouting(true)
        rerouteJob =
            scope.launch {
                val result = routing.route(RouteRequest(fix.at, stops, mode), prefer = old.backend)
                result.skipped.forEach { log("reroute: ${it.backend} skipped: ${it.reason} ${it.detail.orEmpty()}") }
                setRerouting(false)
                val route = result.value
                if (route == null) {
                    log("reroute: no backend could route; still off the route")
                } else {
                    log("reroute: ${route.backend}, ${route.distanceM.toInt()} m")
                    start(route, RouteRequest(fix.at, stops, mode))
                }
            }
    }

    private fun setRerouting(on: Boolean) {
        rerouting = on
        _state.value = _state.value?.copy(rerouting = on)
    }

    companion object {
        /** Off the route this long (by the fixes' clock) before rerouting: a GPS jump isn't a detour. */
        const val OFF_ROUTE_MS = 3_000L

        /** At most one reroute this often: quotas, and FOSSGIS's one request a second. */
        const val MIN_REROUTE_MS = 10_000L
    }
}
