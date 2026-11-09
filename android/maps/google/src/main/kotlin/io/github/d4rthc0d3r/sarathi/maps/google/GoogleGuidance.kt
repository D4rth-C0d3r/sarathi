package io.github.d4rthc0d3r.sarathi.maps.google

import android.content.Context
import com.google.android.libraries.mapsplatform.turnbyturn.model.NavInfo
import com.google.android.libraries.mapsplatform.turnbyturn.model.NavState
import com.google.android.libraries.navigation.AudioGuidanceSettings
import com.google.android.libraries.navigation.Navigator
import com.google.android.libraries.navigation.SimulationOptions
import io.github.d4rthc0d3r.sarathi.core.maps.Fix
import io.github.d4rthc0d3r.sarathi.core.maps.GuidanceEngine
import io.github.d4rthc0d3r.sarathi.core.maps.GuidanceState
import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.maps.Route
import io.github.d4rthc0d3r.sarathi.core.maps.StepManeuver
import io.github.d4rthc0d3r.sarathi.core.maps.TravelMode
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.launch

/**
 * Google's own guidance (ADR-0006), headless: guidance starts on the route the routing step set
 * (no second, billed request), the turn-by-turn feed ([GoogleNavUpdatesService]) says where the
 * rider is, and Google reroutes by itself, for free. Voice is off (audio stays on the helmet
 * intercom) and so are heads-up notifications. With [simulate], Google's simulator drives along
 * the route at [speedMultiplier], for desk tests.
 */
class GoogleGuidance(
    private val context: Context,
    private val routing: GoogleNavRouting,
    private val simulate: Boolean = false,
    private val speedMultiplier: Float = 5f,
) : GuidanceEngine {
    override val state = MutableStateFlow<GuidanceState?>(null)
    override val reroutesItself = true
    override val tracksItsOwnPosition = true

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private var job: Job? = null
    private var navigator: Navigator? = null
    private val arrival =
        Navigator.ArrivalListener {
            state.value =
                state.value?.copy(arrived = true, distanceToManeuverM = 0.0)
        }
    private val changes = MutableSharedFlow<Route>(extraBufferCapacity = 4)
    private var mode = TravelMode.TWO_WHEELER

    /** Google's own reroutes, as new routes for the dash. */
    override val routeChanges: Flow<Route> = changes
    private val routeChanged =
        Navigator.RouteChangedListener { navigator?.let { routing.routeNow(it, mode) }?.let { changes.tryEmit(it) } }

    override fun start(route: Route) {
        mode = route.mode
        job?.cancel()
        GoogleNavFeed.clear()
        job =
            scope.launch {
                val nav = routing.navigatorNow() ?: return@launch
                navigator = nav
                nav.registerServiceForNavUpdates(context.packageName, GoogleNavUpdatesService::class.java.name, 1)
                nav.setHeadsUpNotificationEnabled(false)
                nav.setAudioGuidanceSettings(
                    AudioGuidanceSettings
                        .builder()
                        .setGuidanceMode(AudioGuidanceSettings.GuidanceMode.SILENT)
                        .setVibrationEnabled(false)
                        .setBluetoothAudioEnabled(false)
                        .build(),
                )
                nav.addArrivalListener(arrival)
                nav.addRouteChangedListener(routeChanged)
                nav.startGuidance()
                if (simulate) {
                    nav.simulator.simulateLocationsAlongExistingRoute(
                        SimulationOptions().speedMultiplier(speedMultiplier),
                    )
                }
                GoogleNavFeed.navInfo.filterNotNull().collect { info -> map(info)?.let { state.value = it } }
            }
    }

    /** Google follows the phone's location itself. */
    override fun update(fix: Fix) = Unit

    override fun stop() {
        job?.cancel()
        navigator?.let {
            if (simulate) it.simulator.unsetUserLocation()
            it.removeArrivalListener(arrival)
            it.removeRouteChangedListener(routeChanged)
            it.stopGuidance()
            it.unregisterServiceForNavUpdates()
        }
        navigator = null
    }

    /** Google reroutes by itself; nothing for [io.github.d4rthc0d3r.sarathi.core.maps.RideGuidance] to ask. */
    override fun remainingStops(): List<LatLng> = emptyList()

    private fun map(info: NavInfo): GuidanceState? {
        val step = info.currentStep ?: return state.value
        val maneuver = GoogleManeuvers.of(step.maneuver)
        val stoppedAtEnd = info.navState == NavState.STOPPED && maneuver == StepManeuver.ARRIVE
        return GuidanceState(
            maneuver = maneuver,
            exit = (step.roundaboutTurnNumber ?: 0).coerceIn(0, MAX_EXIT),
            distanceToManeuverM = (info.distanceToCurrentStepMeters ?: 0).toDouble(),
            roadName = step.simpleRoadName ?: step.fullRoadName.orEmpty(),
            remainingS = (info.timeToFinalDestinationSeconds ?: 0).toLong(),
            remainingM = (info.distanceToFinalDestinationMeters ?: 0).toDouble(),
            rerouting = info.navState == NavState.REROUTING,
            arrived = state.value?.arrived == true || stoppedAtEnd,
            stepIndex = step.stepNumber ?: 0,
        )
    }

    private companion object {
        const val MAX_EXIT = 15 // the protocol's limit
    }
}
