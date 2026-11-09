package io.github.d4rthc0d3r.sarathi.maps.mappls

import android.content.Context
import com.mappls.sdk.geojson.Point
import com.mappls.sdk.services.api.directions.DirectionsCriteria
import com.mappls.sdk.services.api.directions.MapplsDirectionManager
import com.mappls.sdk.services.api.directions.MapplsDirections
import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.maps.Osrm
import io.github.d4rthc0d3r.sarathi.core.maps.OsrmStep
import io.github.d4rthc0d3r.sarathi.core.maps.ProviderException
import io.github.d4rthc0d3r.sarathi.core.maps.Route
import io.github.d4rthc0d3r.sarathi.core.maps.RouteRequest
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingProvider
import io.github.d4rthc0d3r.sarathi.core.maps.TravelMode
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable

/**
 * Mappls routing (ADR-0006): its Route API through the SDK, OSRM-format answers. Two-wheeler is
 * the `biking` profile, which Mappls offers only with the `route_adv` resource.
 */
class MapplsRouting(
    context: Context,
    private val configured: Boolean,
) : RoutingProvider {
    override val backend = RoutingBackend.MAPPLS

    private val appContext = context.applicationContext

    override fun unavailable(): Unavailable? = if (configured) null else Unavailable.NO_KEY

    override suspend fun route(request: RouteRequest): Route {
        val directions =
            MapplsDirections.builder().run {
                origin(request.origin.point())
                destination(request.stops.last().point())
                request.stops.dropLast(1).forEach { addWaypoint(it.point()) }
                profile(
                    if (request.mode ==
                        TravelMode.TWO_WHEELER
                    ) {
                        DirectionsCriteria.PROFILE_BIKING
                    } else {
                        DirectionsCriteria.PROFILE_DRIVING
                    },
                )
                resource(DirectionsCriteria.RESOURCE_ROUTE)
                geometries(DirectionsCriteria.GEOMETRY_POLYLINE6)
                overview(DirectionsCriteria.OVERVIEW_FULL)
                steps(true)
                alternatives(false)
                build()
            }
        val route =
            MapplsSdk
                .call(appContext) { MapplsDirectionManager.newInstance(directions).executeCall() }
                ?.routes()
                ?.firstOrNull()
                ?: throw ProviderException(Unavailable.NO_ROUTE, "Mappls found no route")
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
        return Osrm.route(backend, legs, route.distance() ?: 0.0, route.duration() ?: 0.0, request.mode, ATTRIBUTION)
    }

    private fun LatLng.point(): Point = Point.fromLngLat(lng, lat)

    companion object {
        const val ATTRIBUTION = "Powered by Mappls"
    }
}
