package io.github.d4rthc0d3r.sarathi.maps.google

import android.app.Activity
import android.app.Application
import android.util.Log
import com.google.android.libraries.navigation.NavigationApi
import com.google.android.libraries.navigation.NavigationRoadStretchRenderingData
import com.google.android.libraries.navigation.NavigationTrafficData
import com.google.android.libraries.navigation.Navigator
import com.google.android.libraries.navigation.RoutingOptions
import com.google.android.libraries.navigation.Waypoint
import io.github.d4rthc0d3r.sarathi.core.maps.Geo
import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.maps.ProviderException
import io.github.d4rthc0d3r.sarathi.core.maps.Route
import io.github.d4rthc0d3r.sarathi.core.maps.RouteRequest
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingProvider
import io.github.d4rthc0d3r.sarathi.core.maps.Traffic
import io.github.d4rthc0d3r.sarathi.core.maps.TrafficLevel
import io.github.d4rthc0d3r.sarathi.core.maps.TrafficStretch
import io.github.d4rthc0d3r.sarathi.core.maps.TrafficUpdate
import io.github.d4rthc0d3r.sarathi.core.maps.TravelMode
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.flowOn
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
            val twoWheeler = request.mode == TravelMode.TWO_WHEELER
            val mode = if (twoWheeler) RoutingOptions.TravelMode.TWO_WHEELER else RoutingOptions.TravelMode.DRIVING
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
                traffic = trafficNow(nav),
            )
        }

    /** Google's traffic updates are free while the route is set (ADR-0006); each comes with the ETA. */
    override fun traffic(route: Route): Flow<TrafficUpdate> =
        callbackFlow {
            val nav = navigator()
            val listener = Navigator.TrafficUpdatedListener { trafficNow(nav)?.let { trySend(it) } }
            nav.addTrafficUpdatedListener(listener)
            awaitClose { nav.removeTrafficUpdatedListener(listener) }
        }.flowOn(Dispatchers.Main)

    /** Stretches per route segment (one per stop), relative to the segment's start. */
    private fun trafficNow(nav: Navigator): TrafficUpdate? {
        val segments = nav.routeSegments
        val data = segments.map { it.trafficData }
        // Logged until a real ride shows when Google sends traffic (ADR-0006, step F6.3).
        Log.i(TAG, "traffic: ${segments.size} segments, status ${data.map { it?.status }}")
        if (segments.isEmpty() || data.any { it?.status != NavigationTrafficData.Status.OK }) return null
        val legLengths = segments.map { s -> Geo.lengthM(s.latLngs.map { LatLng(it.latitude, it.longitude) }) }
        val legs =
            data.map { d ->
                d!!.roadStretchRenderingDataList.map {
                    TrafficStretch(it.offsetMeters.toDouble(), it.lengthMeters.toDouble(), level(it.style))
                }
            }
        return TrafficUpdate(Traffic.fromLegs(legLengths, legs), nav.currentTimeAndDistance?.seconds?.toLong())
    }

    private fun level(style: NavigationRoadStretchRenderingData.Style): TrafficLevel =
        when (style) {
            NavigationRoadStretchRenderingData.Style.SLOWER_TRAFFIC -> TrafficLevel.SLOW
            NavigationRoadStretchRenderingData.Style.TRAFFIC_JAM -> TrafficLevel.JAM
            else -> TrafficLevel.UNKNOWN
        }

    /** The navigator, for guidance on the route this set; null if it can't be had. */
    internal suspend fun navigatorNow(): Navigator? = runCatching { navigator() }.getOrNull()

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
        private const val TAG = "GoogleNav"

        fun termsAccepted(application: Application): Boolean = NavigationApi.areTermsAccepted(application)

        /** Google's terms dialog; each user has to accept it once before Google can route. */
        fun askForTerms(
            activity: Activity,
            onResult: (Boolean) -> Unit,
        ) = NavigationApi.showTermsAndConditionsDialog(activity, "sārathi") { onResult(it) }
    }
}
