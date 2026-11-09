package io.github.d4rthc0d3r.sarathi.link

import android.content.Context
import android.os.SystemClock
import android.util.Log
import io.github.d4rthc0d3r.sarathi.core.maps.Route
import io.github.d4rthc0d3r.sarathi.core.maps.RouteSimulator
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend
import io.github.d4rthc0d3r.sarathi.maps.MapsRegistry
import io.github.d4rthc0d3r.sarathi.maps.google.GoogleGuidance
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.launch

/**
 * A desk-test ride's positions (step F8): from the route simulator, 15 m apart and that many
 * more a second at a higher speed-up (Ferrostar follows a route step by step, so positions
 * mustn't jump); after a reroute, along the new route. For Google, from its own simulator
 * through the road-snapped location. They replace GPS for the ride and reach the dash as poses.
 */
internal object RideSimulation {
    private const val TAG = "Ride"
    private const val SPEED_MPS = 15.0 // 54 km/h, before the speed-up
    private const val FIX_MS = 1_000L
    private const val DETOUR_AT = 0.15
    private const val DETOUR_SECONDS = 8
    private const val DETOUR_OFFSET_M = 120.0

    fun start(
        context: Context,
        pose: PoseSender,
        scope: CoroutineScope,
        routes: StateFlow<Route?>,
        speedup: Int,
    ): Job {
        pose.simulate(true)
        Log.i(TAG, "simulated ride at ${speedup}x")
        return scope.launch {
            if (routes.value?.backend == RoutingBackend.GOOGLE) {
                GoogleGuidance.roadSnappedFixes(context).collect { pose.inject(it) }
            } else {
                val intervalMs = FIX_MS / speedup
                val drill = MapsRegistry.get(context).drill
                routes.filterNotNull().collectLatest { route ->
                    // The failure drill needs reroutes: leave the road while it has stages left.
                    // Long enough to be off the route for a reroute (3 s) at any speed-up.
                    val detourM = SPEED_MPS * speedup * DETOUR_SECONDS
                    val detour = RouteSimulator.Detour(route.distanceM * DETOUR_AT, detourM, DETOUR_OFFSET_M)
                    val sim =
                        RouteSimulator(
                            route.points,
                            SPEED_MPS * speedup,
                            intervalMs,
                            detour = detour.takeIf { drill.reroutesDue },
                        )
                    for (fix in sim.fixes()) {
                        pose.inject(fix.copy(timeMs = SystemClock.elapsedRealtime()))
                        delay(intervalMs)
                    }
                }
            }
        }
    }
}
