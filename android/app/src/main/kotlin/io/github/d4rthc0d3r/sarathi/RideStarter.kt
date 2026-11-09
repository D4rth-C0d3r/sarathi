package io.github.d4rthc0d3r.sarathi

import android.content.Context
import android.widget.Toast
import io.github.d4rthc0d3r.sarathi.core.ble.LinkState
import io.github.d4rthc0d3r.sarathi.core.maps.DeskRecording
import io.github.d4rthc0d3r.sarathi.core.maps.GuidanceEngine
import io.github.d4rthc0d3r.sarathi.core.maps.Route
import io.github.d4rthc0d3r.sarathi.core.maps.RouteRequest
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingService
import io.github.d4rthc0d3r.sarathi.feature.destination.RouteViewModel
import io.github.d4rthc0d3r.sarathi.feature.destination.StartChecks
import io.github.d4rthc0d3r.sarathi.feature.navigation.FerrostarGuidance
import io.github.d4rthc0d3r.sarathi.link.ActiveRide
import io.github.d4rthc0d3r.sarathi.link.LinkRepository
import io.github.d4rthc0d3r.sarathi.link.RideLinkService
import io.github.d4rthc0d3r.sarathi.link.RideNavigation
import io.github.d4rthc0d3r.sarathi.link.RidePlan
import io.github.d4rthc0d3r.sarathi.maps.DeskRecordings
import io.github.d4rthc0d3r.sarathi.maps.MapsRegistry
import io.github.d4rthc0d3r.sarathi.maps.MapsStore
import io.github.d4rthc0d3r.sarathi.maps.google.GoogleGuidance
import io.github.d4rthc0d3r.sarathi.ui.MeterNames
import io.github.d4rthc0d3r.sarathi.ui.ReplayInfo
import io.github.d4rthc0d3r.sarathi.ui.SetupState
import io.github.d4rthc0d3r.sarathi.ui.SetupStep

/** Starting a confirmed ride (step F7, ADR-0008), and what the confirmation checks first. */
object RideStarter {
    /** Starts the routed result as the ride, in the ride service; false if there's nothing routed. */
    fun start(
        context: Context,
        routeModel: RouteViewModel,
        destination: String,
        simulation: Int? = null,
    ): Boolean {
        val result = routeModel.ui.value.result
        val route = result?.value
        val request = routeModel.lastRequest
        val ok = result != null && route != null && request != null
        if (ok) {
            ActiveRide.start(RidePlan(route, request, result.skipped, destination, simulation))
            RideLinkService.start(context)
            Toast.makeText(context, R.string.ride_started, Toast.LENGTH_LONG).show()
        }
        return ok
    }

    /** Keeps the routed result for desk tests (step F8); Google routes aren't kept (its terms). */
    fun record(
        context: Context,
        routeModel: RouteViewModel,
        destination: String,
    ) {
        val route =
            routeModel.ui.value.result
                ?.value
        val request = routeModel.lastRequest
        val message =
            when {
                route == null || request == null -> {
                    R.string.record_nothing
                }

                !DeskRecording.canRecord(route) -> {
                    R.string.record_not_google
                }

                else -> {
                    DeskRecordings.save(context, DeskRecording(destination, route, request, System.currentTimeMillis()))
                    R.string.record_saved
                }
            }
        Toast.makeText(context, message, Toast.LENGTH_LONG).show()
    }

    /** Replays the desk-test recording as a simulated ride with scripted traffic (step F8). */
    fun replay(
        context: Context,
        speedup: Int,
    ) {
        val r = DeskRecordings.load(context) ?: return
        ActiveRide.start(RidePlan(r.route, r.request, emptyList(), r.destination, speedup, scriptedTraffic = true))
        RideLinkService.start(context)
    }

    /** What Home shows of the desk-test recording; debug builds only. */
    fun replayInfo(context: Context): ReplayInfo? =
        if (BuildConfig.DEBUG) {
            DeskRecordings.load(context)?.let {
                ReplayInfo(it.destination, RideNavigation.backendName(it.route.backend), it.route.distanceM / M_PER_KM)
            }
        } else {
            null
        }

    fun checks(
        context: Context,
        setup: SetupState,
        link: LinkRepository.Status,
        backend: RoutingBackend?,
    ): StartChecks {
        val cost = backend?.let { RoutingService.meterOf(it) }?.let { MapsStore.get(context).guard.status(it) }
        return StartChecks(
            dashPaired = setup.dashAddress != null,
            dashConnected = link.link is LinkState.Ready,
            backgroundLocation = SetupStep.BACKGROUND_LOCATION in setup.done,
            batteryExempt = SetupStep.BATTERY in setup.done,
            costLine = cost?.let { context.getString(MeterNames.of(it.meter), it.used, it.cap) },
            canSimulate = BuildConfig.DEBUG,
        )
    }

    /** The desk test's engines: Google's guidance drives Google's own simulator along the route. */
    fun deskEngine(context: Context): (Route, RouteRequest) -> GuidanceEngine =
        { route, request ->
            if (route.backend == RoutingBackend.GOOGLE) {
                GoogleGuidance(
                    context,
                    MapsRegistry.get(context).googleRouting,
                    simulate = true,
                    speedMultiplier = DESK_SPEEDUP,
                )
            } else {
                FerrostarGuidance(request)
            }
        }

    private const val DESK_SPEEDUP = 10f
    private const val M_PER_KM = 1000.0
}
