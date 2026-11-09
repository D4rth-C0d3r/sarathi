package io.github.d4rthc0d3r.sarathi.core.maps

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.emptyFlow

/** A position fix, from the phone's GPS or the [RouteSimulator]. */
data class Fix(
    val at: LatLng,
    val timeMs: Long,
    val speedMps: Double? = null,
    val bearingDeg: Double? = null,
    val accuracyM: Double = 5.0,
)

/**
 * What the rider needs to know now, whatever the backend (ADR-0006): the protocol's `nav`
 * message, field for field (protocol section 4).
 */
data class GuidanceState(
    val maneuver: StepManeuver,
    val exit: Int = 0,
    val distanceToManeuverM: Double,
    /** The road after the maneuver; may be empty. */
    val roadName: String,
    /** Traffic-aware where the backend has traffic. */
    val remainingS: Long,
    val remainingM: Double,
    val offRoute: Boolean = false,
    val rerouting: Boolean = false,
    val arrived: Boolean = false,
    /** Index into the route's steps of the maneuver ahead, to show step changes. */
    val stepIndex: Int = 0,
)

/**
 * Turn-by-turn guidance along one route (ADR-0006): Ferrostar for Mappls and OSM, the Navigation
 * SDK for Google. Fed with fixes, it says where the rider is on the route and whether they've left it.
 */
interface GuidanceEngine {
    val state: StateFlow<GuidanceState?>

    /** Starts on [route]; a reroute calls this again with the new route. */
    fun start(route: Route)

    /** A new position. Engines that track position themselves (Google) ignore it. */
    fun update(fix: Fix)

    fun stop()

    /** True when the engine reroutes by itself (Google); otherwise [RideGuidance] does. */
    val reroutesItself: Boolean get() = false

    /** True when the engine follows the phone's location itself (Google) and ignores [update]. */
    val tracksItsOwnPosition: Boolean get() = false

    /** The stops not reached yet, last the destination: where a reroute goes. */
    fun remainingStops(): List<LatLng>

    /** New routes from engines that reroute themselves (Google), for the dash. */
    val routeChanges: Flow<Route> get() = emptyFlow()
}
