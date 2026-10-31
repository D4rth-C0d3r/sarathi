package io.github.d4rthc0d3r.sarathi.core.maps

import kotlinx.serialization.Serializable

/** WGS84, degrees. Stays on the phone: the dash only sees ride-frame metres (ADR-0009). */
@Serializable
data class LatLng(
    val lat: Double,
    val lng: Double,
)

/** Search backends (ADR-0006). OSM search is Photon. */
@Serializable
enum class SearchBackend { GOOGLE, MAPPLS, OSM }

/** Routing backends, in the order of the default fallback chain. OSM offline is always last. */
@Serializable
enum class RoutingBackend { GOOGLE, MAPPLS, OSM_VALHALLA, OSM_GRAPHHOPPER, OSM_OFFLINE }

/** Where a place came from: Google's coordinates may only be kept 30 days (ADR-0008). */
@Serializable
enum class PlaceSource { GOOGLE, MAPPLS, OSM, SHARED, PICKED }

@Serializable
data class Place(
    val name: String,
    val address: String = "",
    val at: LatLng? = null,
    val source: PlaceSource,
    /** Google place ID, Mappls Pin (eLoc), or OSM id: enough to look the place up again. */
    val id: String? = null,
)

/** One autocomplete row; [details] gives the [Place] with coordinates. */
data class Suggestion(
    val title: String,
    val subtitle: String,
    val backend: SearchBackend,
    val id: String,
    /** Set when the backend already gave coordinates (Photon, Mappls with a Pin). */
    val at: LatLng? = null,
)

/** Google bills autocomplete per session; a session ends with one details call (ADR-0006). */
class SearchSession(
    val token: String,
)

enum class TravelMode { TWO_WHEELER, DRIVING }

data class RouteRequest(
    val origin: LatLng,
    /** In order; the last is the destination. */
    val stops: List<LatLng>,
    val mode: TravelMode = TravelMode.TWO_WHEELER,
)

/** Maneuver of a route step, as the protocol names them (protocol.md section 3). */
enum class StepManeuver {
    DEPART,
    STRAIGHT,
    SLIGHT_LEFT,
    LEFT,
    SHARP_LEFT,
    SLIGHT_RIGHT,
    RIGHT,
    SHARP_RIGHT,
    UTURN,
    ROUNDABOUT,
    ARRIVE,
    KEEP_LEFT,
    KEEP_RIGHT,
    MERGE,
    FORK_LEFT,
    FORK_RIGHT,
    RAMP_LEFT,
    RAMP_RIGHT,
    FERRY,
}

data class RouteStep(
    val maneuver: StepManeuver,
    /** Index into [Route.points] where the maneuver happens. */
    val pointIndex: Int,
    val roadName: String,
    val distanceM: Double,
    val roundaboutExit: Int = 0,
)

enum class TrafficLevel { FREE, SLOW, JAM, UNKNOWN }

/** Traffic along a route: stretches by distance from its start; gaps are unknown. */
data class TrafficStretch(
    val offsetM: Double,
    val lengthM: Double,
    val level: TrafficLevel,
)

data class TrafficUpdate(
    val stretches: List<TrafficStretch>,
    /** Remaining time with traffic, if the backend has it. */
    val etaS: Long? = null,
)

data class Route(
    val backend: RoutingBackend,
    val points: List<LatLng>,
    val steps: List<RouteStep>,
    val distanceM: Double,
    val durationS: Long,
    val mode: TravelMode,
    val attribution: String,
    val traffic: TrafficUpdate? = null,
    /** The main road, for "via NH48" on the confirmation screen. */
    val via: String = "",
)
