package io.github.d4rthc0d3r.sarathi.maps.google

import android.app.Activity
import android.app.Application
import com.google.android.libraries.navigation.NavigationApi
import com.google.android.libraries.navigation.Navigator
import com.google.android.libraries.navigation.RoutingOptions
import com.google.android.libraries.navigation.Waypoint
import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.maps.ProviderException
import io.github.d4rthc0d3r.sarathi.core.maps.Route
import io.github.d4rthc0d3r.sarathi.core.maps.RouteRequest
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingProvider
import io.github.d4rthc0d3r.sarathi.core.maps.TravelMode
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import com.google.android.libraries.navigation.Navigator.RouteStatus as Status

/**
 * Google routing through the Navigation SDK (ADR-0006): two-wheeler where Google has it, from the
 * phone's own position (the SDK has no other origin). Each request bills one destination per stop,
 * which the routing service counts first. The destinations stay set, so guidance (step F6) can
 * start on this route without a second, billed request. Steps come from guidance's feed, so a
 * route here has the line, distance and time but no steps yet.
 */
class GoogleNavRouting(
    private val application: Application,
    private val apiKey: String?,
) : RoutingProvider {
    override val backend = RoutingBackend.GOOGLE
    override val hasTraffic = true

    private var navigator: Navigator? = null

    override fun unavailable(): Unavailable? =
        when {
            apiKey.isNullOrEmpty() -> Unavailable.NO_KEY
            !termsAccepted(application) -> Unavailable.NO_TERMS
            else -> null
        }

    override suspend fun route(request: RouteRequest): Route =
        withContext(Dispatchers.Main) {
            val nav = navigator()
            val waypoints = request.stops.map { Waypoint.builder().setLatLng(it.lat, it.lng).build() }
            val mode =
                if (request.mode ==
                    TravelMode.TWO_WHEELER
                ) {
                    RoutingOptions.TravelMode.TWO_WHEELER
                } else {
                    RoutingOptions.TravelMode.DRIVING
                }
            val status =
                suspendCancellableCoroutine { cont ->
                    nav.setDestinations(waypoints, RoutingOptions().travelMode(mode)) { cont.resume(it.routeStatus) }
                }
            if (status != Status.OK) throw ProviderException(reason(status), "Google: $status")
            val time =
                nav.currentTimeAndDistance ?: throw ProviderException(Unavailable.ERROR, "Google: no time and distance")
            Route(
                backend = backend,
                points = nav.routeSegments.flatMap { s -> s.latLngs.map { LatLng(it.latitude, it.longitude) } },
                steps = emptyList(),
                distanceM = time.meters.toDouble(),
                durationS = time.seconds.toLong(),
                mode = request.mode,
                attribution = ATTRIBUTION,
            )
        }

    /** One navigator for the app; Google refuses one until the terms are accepted. */
    private suspend fun navigator(): Navigator =
        navigator ?: suspendCancellableCoroutine { cont ->
            NavigationApi.getNavigator(
                application,
                object : NavigationApi.NavigatorListener {
                    override fun onNavigatorReady(n: Navigator) {
                        navigator = n
                        cont.resume(n)
                    }

                    override fun onError(code: Int) =
                        cont.resumeWithException(ProviderException(navigatorError(code), "Google navigator: $code"))
                },
            )
        }

    private fun reason(status: Status): Unavailable =
        when (status) {
            Status.NO_ROUTE_FOUND, Status.WAYPOINT_ERROR, Status.DUPLICATE_WAYPOINTS_ERROR -> Unavailable.NO_ROUTE

            Status.NETWORK_ERROR -> Unavailable.OFFLINE

            Status.QUOTA_CHECK_FAILED -> Unavailable.ACCOUNT

            // key, billing or quota on Google's side
            Status.ROUTING_TIMEOUT -> Unavailable.TIMEOUT

            else -> Unavailable.ERROR // location off or unknown, cancelled
        }

    private fun navigatorError(code: Int): Unavailable =
        when (code) {
            NavigationApi.ErrorCode.NOT_AUTHORIZED -> Unavailable.ACCOUNT
            NavigationApi.ErrorCode.TERMS_NOT_ACCEPTED -> Unavailable.NO_TERMS
            NavigationApi.ErrorCode.NETWORK_ERROR -> Unavailable.OFFLINE
            else -> Unavailable.ERROR
        }

    companion object {
        const val ATTRIBUTION = "Google"

        fun termsAccepted(application: Application): Boolean = NavigationApi.areTermsAccepted(application)

        /** Google's terms dialog; each user has to accept it once before Google can route. */
        fun askForTerms(
            activity: Activity,
            onResult: (Boolean) -> Unit,
        ) = NavigationApi.showTermsAndConditionsDialog(activity, "sārathi") { onResult(it) }
    }
}
