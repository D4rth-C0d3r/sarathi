package io.github.d4rthc0d3r.sarathi.maps.mappls

import android.content.Context
import com.mappls.sdk.geojson.Point
import com.mappls.sdk.services.api.directions.DirectionsCriteria
import com.mappls.sdk.services.api.directions.MapplsDirectionManager
import com.mappls.sdk.services.api.directions.MapplsDirections
import com.mappls.sdk.services.api.directions.models.DirectionsRoute
import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.maps.Osrm
import io.github.d4rthc0d3r.sarathi.core.maps.OsrmStep
import io.github.d4rthc0d3r.sarathi.core.maps.ProviderException
import io.github.d4rthc0d3r.sarathi.core.maps.Route
import io.github.d4rthc0d3r.sarathi.core.maps.RouteRequest
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingProvider
import io.github.d4rthc0d3r.sarathi.core.maps.Traffic
import io.github.d4rthc0d3r.sarathi.core.maps.TrafficUpdate
import io.github.d4rthc0d3r.sarathi.core.maps.TravelMode
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable

/**
 * Mappls routing (ADR-0006): its Route API through the SDK, OSRM-format answers. Two-wheeler is
 * the `biking` profile; with `route_traffic` it also gives per-segment congestion and a
 * traffic-aware duration (checked on the phone, 2026-11-09). That traffic isn't refreshed: the
 * free plan's answers carry no request or route ID for Mappls's route-refresh call (also checked
 * on the phone), so the dash shows it as unknown once it's 5 minutes old (protocol section 5).
 */
class MapplsRouting(
    context: Context,
    private val configured: Boolean,
) : RoutingProvider {
    override val backend = RoutingBackend.MAPPLS
    override val hasTraffic = true

    private val appContext = context.applicationContext

    override fun unavailable(): Unavailable? = if (configured) null else Unavailable.NO_KEY

    override suspend fun route(request: RouteRequest): Route {
        val profile =
            if (request.mode ==
                TravelMode.TWO_WHEELER
            ) {
                DirectionsCriteria.PROFILE_BIKING
            } else {
                DirectionsCriteria.PROFILE_DRIVING
            }
        val directions =
            MapplsDirections.builder().run {
                origin(request.origin.point())
                destination(request.stops.last().point())
                request.stops.dropLast(1).forEach { addWaypoint(it.point()) }
                profile(profile)
                resource(DirectionsCriteria.RESOURCE_ROUTE_TRAFFIC)
                annotations(DirectionsCriteria.ANNOTATION_CONGESTION, DirectionsCriteria.ANNOTATION_DISTANCE)
                geometries(DirectionsCriteria.GEOMETRY_POLYLINE6)
                overview(DirectionsCriteria.OVERVIEW_FULL)
                steps(true)
                alternatives(false)
                build()
            }
        val response = MapplsSdk.call(appContext) { MapplsDirectionManager.newInstance(directions).executeCall() }
        val route =
            response?.routes()?.firstOrNull() ?: throw ProviderException(Unavailable.NO_ROUTE, "Mappls found no route")
        val legs =
            route.legs().orEmpty().map { leg ->
                leg.steps().orEmpty().map { s ->
                    val m = s.maneuver()
                    OsrmStep(
                        m.type().orEmpty(),
                        m.modifier(),
                        m.exit(),
                        s.name().orEmpty(),
                        s.distance(),
                        s.geometry().orEmpty(),
                    )
                }
            }
        return Osrm
            .route(backend, legs, route.distance() ?: 0.0, route.duration() ?: 0.0, request.mode, ATTRIBUTION)
            .copy(traffic = trafficOf(route))
    }

    /** Per-segment congestion and distance, per leg, joined into one route. */
    private fun trafficOf(route: DirectionsRoute): TrafficUpdate? {
        val annotations = route.legs().orEmpty().mapNotNull { it.annotation() }
        val lengths = annotations.flatMap { it.distance().orEmpty() }
        val levels = annotations.flatMap { a -> a.congestion().orEmpty().map(Traffic::mapplsLevel) }
        val complete =
            annotations.size == route.legs().orEmpty().size && lengths.isNotEmpty() && lengths.size == levels.size
        return if (complete) TrafficUpdate(Traffic.fromSegments(lengths, levels), route.duration()?.toLong()) else null
    }

    private fun LatLng.point(): Point = Point.fromLngLat(lng, lat)

    companion object {
        const val ATTRIBUTION = "Powered by Mappls"
    }
}
