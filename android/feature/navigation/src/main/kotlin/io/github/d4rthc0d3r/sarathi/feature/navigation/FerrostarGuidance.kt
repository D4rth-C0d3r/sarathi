package io.github.d4rthc0d3r.sarathi.feature.navigation

import io.github.d4rthc0d3r.sarathi.core.maps.Fix
import io.github.d4rthc0d3r.sarathi.core.maps.GuidanceEngine
import io.github.d4rthc0d3r.sarathi.core.maps.GuidanceState
import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.maps.OsrmJson
import io.github.d4rthc0d3r.sarathi.core.maps.Route
import io.github.d4rthc0d3r.sarathi.core.maps.RouteRequest
import io.github.d4rthc0d3r.sarathi.core.maps.StepManeuver
import kotlinx.coroutines.flow.MutableStateFlow
import uniffi.ferrostar.CourseFiltering
import uniffi.ferrostar.CourseOverGround
import uniffi.ferrostar.GeographicCoordinate
import uniffi.ferrostar.NavState
import uniffi.ferrostar.NavigationControllerConfig
import uniffi.ferrostar.Navigator
import uniffi.ferrostar.RouteDeviation
import uniffi.ferrostar.RouteDeviationTracking
import uniffi.ferrostar.Speed
import uniffi.ferrostar.TripState
import uniffi.ferrostar.UserLocation
import uniffi.ferrostar.Waypoint
import uniffi.ferrostar.WaypointAdvanceMode
import uniffi.ferrostar.WaypointKind
import uniffi.ferrostar.createNavigator
import uniffi.ferrostar.createRouteFromOsrmRoute
import uniffi.ferrostar.stepAdvanceDistanceEntryAndExit
import uniffi.ferrostar.stepAdvanceDistanceToEndOfStep
import java.time.Instant

/**
 * Guidance with Ferrostar's navigation controller (ADR-0006) for Mappls and OSM routes: our route
 * goes in as OSRM JSON ([OsrmJson]), fixes go in one by one, and Ferrostar's progress comes back
 * mapped onto our own steps. Only the controller is used; routing and location are ours.
 */
class FerrostarGuidance(
    private val request: RouteRequest,
) : GuidanceEngine {
    override val state = MutableStateFlow<GuidanceState?>(null)

    private var route: Route? = null
    private var navigator: Navigator? = null
    private var nav: NavState? = null

    override fun start(route: Route) {
        this.route = route
        val waypoints =
            (
                listOf(
                    request.origin,
                ) + request.stops
            ).map { Waypoint(it.coordinate(), WaypointKind.BREAK, null) }
        val ferrostarRoute =
            createRouteFromOsrmRoute(OsrmJson.route(route).toString().encodeToByteArray(), waypoints, POLYLINE6)
        navigator = createNavigator(ferrostarRoute, CONFIG, false)
        nav = null
        // Before the first fix: the route's first maneuver after departing, at its full distance.
        val next = route.steps.getOrNull(1) ?: route.steps.lastOrNull()
        state.value =
            GuidanceState(
                maneuver = next?.maneuver ?: StepManeuver.ARRIVE,
                exit = next?.roundaboutExit ?: 0,
                distanceToManeuverM = route.steps.firstOrNull()?.distanceM ?: route.distanceM,
                roadName = next?.roadName.orEmpty(),
                remainingS = route.traffic?.etaS ?: route.durationS,
                remainingM = route.distanceM,
                stepIndex = 1,
            )
    }

    override fun update(fix: Fix) {
        val n = navigator ?: return
        val location = fix.location()
        val next = nav?.let { n.updateUserLocation(location, it) } ?: n.getInitialState(location)
        nav = next
        map(next.tripState)?.let { state.value = it }
    }

    override fun stop() {
        navigator = null
        nav = null
    }

    override fun remainingStops(): List<LatLng> {
        val trip = nav?.tripState as? TripState.Navigating
        val remaining = trip?.remainingWaypoints?.map { LatLng(it.coordinate.lat, it.coordinate.lng) }.orEmpty()
        return remaining.ifEmpty { request.stops.takeLast(1) }
    }

    /** Ferrostar's trip state on our steps: step i's maneuver is where step i starts. */
    private fun map(trip: TripState): GuidanceState? {
        val r = route ?: return null
        return when (trip) {
            is TripState.Navigating -> {
                val current = (r.steps.size - trip.remainingSteps.size).coerceIn(0, r.steps.lastIndex)
                val nextIndex = (current + 1).coerceAtMost(r.steps.lastIndex)
                val next = r.steps[nextIndex]
                GuidanceState(
                    maneuver = next.maneuver,
                    exit = next.roundaboutExit,
                    distanceToManeuverM = trip.progress.distanceToNextManeuver,
                    roadName = next.roadName,
                    remainingS = trip.progress.durationRemaining.toLong(),
                    remainingM = trip.progress.distanceRemaining,
                    offRoute = trip.deviation is RouteDeviation.Deviation,
                    stepIndex = nextIndex,
                )
            }

            is TripState.Complete -> {
                state.value?.copy(
                    arrived = true,
                    distanceToManeuverM = 0.0,
                    remainingM = 0.0,
                    remainingS = 0,
                    offRoute = false,
                )
            }

            else -> {
                null
            }
        }
    }

    private fun Fix.location() =
        UserLocation(
            at.coordinate(),
            accuracyM,
            bearingDeg?.let { CourseOverGround(it.toInt().mod(FULL_TURN).toUShort(), null) },
            Instant.ofEpochMilli(timeMs),
            speedMps?.let { Speed(it, null) },
        )

    private fun LatLng.coordinate() = GeographicCoordinate(lat, lng)

    private companion object {
        const val FULL_TURN = 360
        const val POLYLINE6: UInt = 6u

        /**
         * Ferrostar's documented starting values: advance a step within 30 m of its end and 5 m
         * past it, with fixes better than 32 m; off the route beyond 50 m with fixes better
         * than 25 m; stops reached within 100 m.
         */
        val CONFIG =
            NavigationControllerConfig(
                WaypointAdvanceMode.WaypointWithinRange(100.0),
                stepAdvanceDistanceEntryAndExit(30u, 5u, 32u),
                stepAdvanceDistanceToEndOfStep(30u, 32u),
                RouteDeviationTracking.StaticThreshold(25u, 50.0),
                CourseFiltering.SNAP_TO_ROUTE,
            )
    }
}
