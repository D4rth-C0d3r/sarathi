package io.github.d4rthc0d3r.sarathi.link

import android.content.Context
import android.util.Log
import io.github.d4rthc0d3r.sarathi.core.ble.DashChannel
import io.github.d4rthc0d3r.sarathi.core.maps.GuidanceEngine
import io.github.d4rthc0d3r.sarathi.core.maps.GuidanceState
import io.github.d4rthc0d3r.sarathi.core.maps.RideGuidance
import io.github.d4rthc0d3r.sarathi.core.maps.Route
import io.github.d4rthc0d3r.sarathi.core.maps.RouteRequest
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingService
import io.github.d4rthc0d3r.sarathi.core.maps.SearchBackend
import io.github.d4rthc0d3r.sarathi.core.maps.TrafficUpdate
import io.github.d4rthc0d3r.sarathi.core.protocol.BinaryMessage
import io.github.d4rthc0d3r.sarathi.core.protocol.FrameEpoch
import io.github.d4rthc0d3r.sarathi.core.protocol.JsonMessage
import io.github.d4rthc0d3r.sarathi.core.protocol.Nav
import io.github.d4rthc0d3r.sarathi.core.protocol.NavEnd
import io.github.d4rthc0d3r.sarathi.core.protocol.NavEndReason
import io.github.d4rthc0d3r.sarathi.core.protocol.RideFrame
import io.github.d4rthc0d3r.sarathi.feature.navigation.FerrostarGuidance
import io.github.d4rthc0d3r.sarathi.maps.MapsRegistry
import io.github.d4rthc0d3r.sarathi.maps.MapsStore
import io.github.d4rthc0d3r.sarathi.maps.google.GoogleGuidance
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.launch
import kotlin.random.Random
import io.github.d4rthc0d3r.sarathi.core.maps.Fix as MapsFix

/**
 * The ride on the dash (protocol section 5, ADR-0007): follows [ActiveRide], runs guidance on
 * the phone's fixes, and streams the ride: backend, route, traffic and nav at start and whenever
 * the dash reconnects or the frame changes; a reroute event and a new route version on reroutes;
 * nav every second and on change; `nav_end` at the end. Runs in the ride service, headless.
 */
class RideNavigation(
    private val context: Context,
    private val dash: DashChannel,
    private val scope: CoroutineScope,
    private val pose: PoseSender,
) {
    private val frame: StateFlow<RideFrame?> = pose.frame
    private val fixes: Flow<MapsFix> = pose.fixes

    private val outbox = Channel<Any>(OUTBOX)
    private val jobs = mutableListOf<Job>()
    private var ride: Ride? = null

    fun start() {
        // One sender, so messages reach the dash in the order they were queued.
        jobs += scope.launch { for (m in outbox) deliver(m) }
        jobs +=
            scope.launch {
                ActiveRide.plan.collect { plan ->
                    ride?.stop(if (plan == null) NavEndReason.CANCELLED else null)
                    ride = plan?.let { Ride(it).apply { run() } }
                }
            }
    }

    fun stop() {
        ride?.stop(null)
        ride = null
        jobs.forEach { it.cancel() }
        jobs.clear()
    }

    private fun queue(message: Any) {
        outbox.trySend(message)
    }

    private suspend fun deliver(message: Any) {
        if (!dash.ready.value) return // the reconnect resend covers it
        runCatching {
            when (message) {
                is JsonMessage -> dash.send(message)
                is BinaryMessage -> dash.send(message)
            }
        }.onFailure { Log.w(TAG, "not sent: ${it.message}") }
    }

    /** One ride: its route id and version, guidance, traffic and the last nav. */
    private inner class Ride(
        private val plan: RidePlan,
    ) {
        private val maps = MapsRegistry.get(context)
        private val routing: RoutingService = maps.routing
        private val routeId = Random.nextLong(1, U32_LIMIT)
        private var version = 0
        private var route = plan.route
        private var traffic = TrafficStream(routeId, version)
        private var lastTraffic: TrafficUpdate? = plan.route.traffic
        private var lastNav: Nav? = null
        private var lastNavAtMs = 0L
        private var ended = false

        /** Nav waits for the ride itself (frame, backend, route, traffic): the protocol's order. */
        private var rideSent = false
        private val guidance = RideGuidance(routing, ::engineFor, scope) { Log.i(TAG, it) }
        private val rideJobs = mutableListOf<Job>()
        private var trafficJob: Job? = null

        fun run() {
            Log.i(TAG, "ride: ${route.backend}, ${route.distanceM.toInt()} m to ${plan.destination}")
            guidance.start(plan.route, plan.request)
            events()
            rideJobs += scope.launch { fixes.collect { guidance.update(it) } }
            rideJobs +=
                scope.launch {
                    guidance.route
                        .drop(1)
                        .filterNotNull()
                        .collect { onReroute(it) }
                }
            rideJobs += scope.launch { guidance.state.filterNotNull().collect { onState(it) } }
            rideJobs += scope.launch { repeatNav() }
            rideJobs +=
                scope.launch {
                    combine(dash.ready, frame) { ready, f -> if (ready) f else null }
                        .distinctUntilChanged { a, b -> a?.epoch == b?.epoch }
                        .filterNotNull()
                        .collect { sendRide(it) }
                }
            followTraffic()
            plan.simulation?.let { rideJobs += RideSimulation.start(context, pose, scope, guidance.route, it) }
        }

        /** Ends the ride; [reason] null when it ended itself (arrival) or the service stops. */
        fun stop(reason: NavEndReason?) {
            if (reason != null && !ended) queue(NavEnd(reason))
            ended = true
            trafficJob?.cancel()
            rideJobs.forEach { it.cancel() }
            guidance.stop()
            pose.simulate(false)
            ActiveRide.publish(null)
        }

        private fun events() {
            RideMessages.fallbacks(plan.skipped, route.backend).forEach(::queue)
            val meter = RoutingService.meterOf(route.backend) ?: return
            val search = if (route.backend == RoutingBackend.GOOGLE) SearchBackend.GOOGLE else SearchBackend.MAPPLS
            RideMessages.costWarning(MapsStore.get(context).guard.status(meter), search)?.let(::queue)
        }

        /** Everything the dash needs to draw the ride, in the protocol's start order. */
        private fun sendRide(f: RideFrame) {
            // The frame first (protocol section 5): the dash refuses a route in a frame it doesn't
            // hold, and the pose that also carries it may be half a minute away indoors.
            queue(FrameEpoch(f.epoch))
            queue(
                RideMessages.backend(
                    route,
                    MapsStore
                        .get(context)
                        .settings.value.search,
                    backendName(route.backend),
                ),
            )
            queue(RideMessages.route(route, f, routeId, version))
            lastTraffic?.let {
                queue(
                    io.github.d4rthc0d3r.sarathi.core.protocol.Traffic(
                        routeId,
                        version,
                        TrafficStream.entries(it.stretches),
                    ),
                )
            }
            lastNav?.let(::queue)
            rideSent = true
        }

        private fun onReroute(r: Route) {
            version = (version + 1) and U16_MASK
            route = r
            traffic = TrafficStream(routeId, version)
            lastTraffic = r.traffic
            Log.i(TAG, "reroute: version $version, ${r.backend}, ${r.distanceM.toInt()} m")
            queue(RideMessages.reroute())
            frame.value?.let { queue(RideMessages.route(r, it, routeId, version)) }
            lastTraffic?.let { traffic.offer(it)?.let(::queue) }
            followTraffic()
        }

        private fun onState(g: GuidanceState) {
            ActiveRide.publish(g)
            val nav = RideMessages.nav(g)
            val previous = lastNav
            lastNav = nav
            val changed =
                previous == null || previous.m != nav.m || previous.street != nav.street || previous.flags != nav.flags
            if (changed && rideSent) sendNav(nav)
            if (g.arrived && !ended) {
                ended = true
                sendNav(nav)
                queue(NavEnd(NavEndReason.ARRIVED))
                Log.i(TAG, "arrived")
                ActiveRide.end()
            }
        }

        /** The protocol's 1 Hz while navigating, between changes. */
        private suspend fun repeatNav() {
            while (true) {
                delay(NAV_PERIOD_MS)
                val nav = lastNav ?: continue
                if (rideSent && System.currentTimeMillis() - lastNavAtMs >= NAV_PERIOD_MS) sendNav(nav)
            }
        }

        private fun sendNav(nav: Nav) {
            lastNavAtMs = System.currentTimeMillis()
            queue(nav)
        }

        /** Traffic updates for the current route; its first traffic goes with the route itself. */
        private fun followTraffic() {
            trafficJob?.cancel()
            trafficJob =
                scope.launch {
                    routing.traffic(route).collect { t ->
                        lastTraffic = t
                        traffic.offer(t)?.let(::queue)
                    }
                }
        }

        private fun engineFor(
            r: Route,
            request: RouteRequest,
        ): GuidanceEngine =
            if (r.backend ==
                RoutingBackend.GOOGLE
            ) {
                GoogleGuidance(context, maps.googleRouting)
            } else {
                FerrostarGuidance(request)
            }
    }

    companion object {
        private const val TAG = "Ride"
        private const val OUTBOX = 64
        private const val NAV_PERIOD_MS = 1_000L
        private const val U32_LIMIT = 0xFFFF_FFFFL
        private const val U16_MASK = 0xFFFF

        /** The backend's name on the dash (24 bytes at most). */
        fun backendName(b: RoutingBackend): String =
            when (b) {
                RoutingBackend.GOOGLE -> "Google"
                RoutingBackend.MAPPLS -> "Mappls"
                RoutingBackend.OSM_VALHALLA -> "OSM (Valhalla)"
                RoutingBackend.OSM_GRAPHHOPPER -> "OSM (GraphHopper)"
                RoutingBackend.OSM_OFFLINE -> "OSM offline"
            }
    }
}
