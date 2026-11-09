package io.github.d4rthc0d3r.sarathi.feature.destination

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import io.github.d4rthc0d3r.sarathi.core.maps.ChainResult
import io.github.d4rthc0d3r.sarathi.core.maps.GuidanceEngine
import io.github.d4rthc0d3r.sarathi.core.maps.GuidanceState
import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.maps.Place
import io.github.d4rthc0d3r.sarathi.core.maps.RideGuidance
import io.github.d4rthc0d3r.sarathi.core.maps.Route
import io.github.d4rthc0d3r.sarathi.core.maps.RouteRequest
import io.github.d4rthc0d3r.sarathi.core.maps.RouteSimulator
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingService
import io.github.d4rthc0d3r.sarathi.core.maps.TrafficUpdate
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancelChildren
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull

data class RouteUi(
    val routing: Boolean = false,
    /** Which backend routed it and which were skipped (ADR-0006). */
    val result: ChainResult<RoutingBackend, Route>? = null,
    /** No current position to start from. */
    val noPosition: Boolean = false,
    /** The latest traffic: the route's own, then the backend's updates (step F5). */
    val traffic: TrafficUpdate? = null,
    val trafficUpdates: Int = 0,
    /** A simulated ride along the route (step F6, desk test). */
    val simulation: Simulation? = null,
)

data class Simulation(
    val running: Boolean = true,
    val guidance: GuidanceState? = null,
    /** Steps on the route being followed; a reroute changes it. */
    val steps: Int = 0,
    val reroutes: Int = 0,
    val lastReroute: String = "",
    /** No guidance engine for this backend yet. */
    val unsupported: Boolean = false,
)

/**
 * A route to the chosen destination or shared stops, for checking backends at the desk. The
 * confirmation screen (step F7) grows from this.
 */
class RouteViewModel(
    private val routing: RoutingService,
    private val near: () -> LatLng?,
    /** The guidance engine for a route's backend; null where there's none yet. */
    private val engineFor: (Route, RouteRequest) -> GuidanceEngine? = { _, _ -> null },
) : ViewModel() {
    private val _ui = MutableStateFlow(RouteUi())
    val ui: StateFlow<RouteUi> = _ui.asStateFlow()
    private var job: Job? = null
    private var simulationJob: Job? = null
    private var request: RouteRequest? = null

    /** What the last route was asked for: the plan the rider starts. */
    val lastRequest: RouteRequest? get() = request

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
        job =
            viewModelScope.launch {
                val result = routing.route(RouteRequest(from, to).also { request = it })
                val route = result.value
                _ui.value = RouteUi(result = result, traffic = route?.traffic)
                route?.let { r ->
                    routing.traffic(r).collect { t ->
                        _ui.update {
                            it.copy(
                                traffic = t,
                                trafficUpdates =
                                    it.trafficUpdates + 1,
                            )
                        }
                    }
                }
            }
    }

    fun clear() {
        job?.cancel()
        simulationJob?.cancel()
        _ui.value = RouteUi()
    }

    /**
     * Rides the route with the [RouteSimulator] at [SPEED_MPS], one fix a simulated second, played
     * [SPEEDUP] times faster than real time; with [detour], leaves the route for a stretch.
     */
    fun simulate(detour: Boolean) {
        val route = _ui.value.result?.value
        val req = request
        if (route == null || req == null) return
        simulationJob?.cancel()
        if (engineFor(route, req) == null) {
            _ui.update { it.copy(simulation = Simulation(running = false, unsupported = true)) }
            return
        }
        val ride =
            RideGuidance(routing, { r, q -> engineFor(r, q)!! }, viewModelScope) { line ->
                _ui.update {
                    it.copy(simulation = it.simulation?.copy(lastReroute = line))
                }
            }
        val sim = simulator(route, detour)
        _ui.update { it.copy(simulation = Simulation(steps = route.steps.size)) }
        simulationJob =
            viewModelScope.launch {
                ride.start(route, req)
                launch {
                    ride.state.filterNotNull().collect { g ->
                        // the last state stays after the ride stops
                        _ui.update { it.copy(simulation = it.simulation?.copy(guidance = g)) }
                    }
                }
                launch {
                    ride.route.drop(1).filterNotNull().collect { r ->
                        // stopping sets it to null: not a reroute
                        _ui.update { u ->
                            u.copy(
                                simulation =
                                    u.simulation?.copy(
                                        reroutes = u.simulation.reroutes + 1,
                                        steps = r.steps.size,
                                    ),
                            )
                        }
                    }
                }
                drive(ride, sim)
                // The last state (arrived) may not have reached the collector before the ride stops.
                val last = ride.state.value
                _ui.update { u ->
                    u.copy(
                        simulation =
                            u.simulation?.copy(
                                running = false,
                                guidance =
                                    last ?: u.simulation.guidance,
                            ),
                    )
                }
                ride.stop()
                coroutineContext.cancelChildren()
            }
    }

    /** Feeds the simulator's fixes, or waits for an engine that drives itself (Google) to arrive. */
    private suspend fun drive(
        ride: RideGuidance,
        sim: RouteSimulator,
    ) {
        if (ride.tracksItsOwnPosition) {
            // Google drives its own simulator along the route: wait for it to arrive.
            withTimeoutOrNull(OWN_SIMULATION_MS) { ride.state.first { it?.arrived == true } }
        } else {
            for (fix in sim.fixes()) {
                ride.update(fix)
                if (ride.state.value?.arrived == true) break
                delay(MS_PER_FIX / SPEEDUP)
            }
        }
    }

    private fun simulator(
        route: Route,
        detour: Boolean,
    ): RouteSimulator {
        val off = RouteSimulator.Detour(route.distanceM * DETOUR_AT, DETOUR_LENGTH_M, DETOUR_OFFSET_M)
        return RouteSimulator(route.points, SPEED_MPS, detour = off.takeIf { detour })
    }

    fun stopSimulation() {
        simulationJob?.cancel()
        _ui.update { it.copy(simulation = it.simulation?.copy(running = false)) }
    }

    private companion object {
        const val SPEED_MPS = 15.0
        const val SPEEDUP = 20
        const val MS_PER_FIX = 1_000L
        const val DETOUR_AT = 0.3
        const val DETOUR_LENGTH_M = 400.0
        const val DETOUR_OFFSET_M = 120.0
        const val OWN_SIMULATION_MS = 15 * 60_000L
    }
}
