package io.github.d4rthc0d3r.sarathi.feature.destination

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.github.d4rthc0d3r.sarathi.core.maps.Route
import io.github.d4rthc0d3r.sarathi.core.maps.RouteStep
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend
import io.github.d4rthc0d3r.sarathi.core.maps.Traffic
import io.github.d4rthc0d3r.sarathi.core.maps.TrafficLevel
import io.github.d4rthc0d3r.sarathi.core.maps.TravelMode
import java.util.Locale

/** The routed result, for the desk: backend, skipped backends, summary and the first steps. */
@Composable
internal fun RoutePreview(
    route: RouteUi,
    actions: DestinationActions = DestinationActions(),
    checks: StartChecks = StartChecks(),
) {
    val small = MaterialTheme.typography.bodySmall
    val result = route.result
    when {
        route.routing -> {
            Text(stringResource(R.string.route_routing), style = small)
        }

        route.noPosition -> {
            Text(stringResource(R.string.route_no_position), color = MaterialTheme.colorScheme.error)
        }

        result == null -> {
            // Nothing routed yet.
        }

        else -> {
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    val r = result.value
                    if (r == null) {
                        Text(stringResource(R.string.route_none), color = MaterialTheme.colorScheme.error)
                    } else {
                        Summary(r)
                        TrafficSummary(route)
                        StartPanel(checks, actions)
                        SimulationPanel(route.simulation, actions)
                    }
                    result.skipped.forEach {
                        Text(
                            stringResource(R.string.dest_skipped, routingName(it.backend), reasonText(it.reason)),
                            style = small,
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun Summary(r: Route) {
    val small = MaterialTheme.typography.bodySmall
    Text(stringResource(R.string.route_with, routingName(r.backend)), style = MaterialTheme.typography.titleMedium)
    Text(stringResource(R.string.route_summary, r.distanceM / M_PER_KM, minutes(r.durationS)))
    if (r.via.isNotBlank()) Text(stringResource(R.string.route_via, r.via))
    if (r.mode == TravelMode.DRIVING) Text(stringResource(R.string.route_car_profile), style = small)
    Text(stringResource(R.string.route_shape, r.points.size, r.steps.size), style = small)
    r.steps.take(PREVIEW_STEPS).forEach { Text(stepLine(it), style = small) }
    Text(r.attribution, style = small)
}

@Composable
private fun TrafficSummary(route: RouteUi) {
    val small = MaterialTheme.typography.bodySmall
    val t = route.traffic
    if (t == null) {
        Text(stringResource(R.string.route_no_traffic), style = small)
        return
    }
    val totals = Traffic.totals(t.stretches)
    val parts =
        listOf(TrafficLevel.FREE, TrafficLevel.SLOW, TrafficLevel.JAM, TrafficLevel.UNKNOWN).mapNotNull { level ->
            totals[level]?.let {
                String.format(
                    Locale.ROOT,
                    "%.1f km %s",
                    it / M_PER_KM,
                    level.name.lowercase(Locale.ROOT),
                )
            }
        }
    Text(stringResource(R.string.route_traffic, parts.joinToString(" · ")), style = small)
    t.etaS?.let { Text(stringResource(R.string.route_traffic_eta, minutes(it), route.trafficUpdates), style = small) }
}

private fun stepLine(s: RouteStep): String {
    val what =
        s.maneuver.name
            .lowercase(Locale.ROOT)
            .replace('_', ' ')
    val exit = if (s.roundaboutExit > 0) " (exit ${s.roundaboutExit})" else ""
    val road = if (s.roadName.isNotBlank()) " · ${s.roadName}" else ""
    return "$what$exit$road · ${s.distanceM.toInt()} m"
}

private fun minutes(seconds: Long) = ((seconds + SECONDS_PER_MINUTE / 2) / SECONDS_PER_MINUTE).toInt()

fun routingName(backend: RoutingBackend): String =
    when (backend) {
        RoutingBackend.GOOGLE -> "Google"
        RoutingBackend.MAPPLS -> "Mappls"
        RoutingBackend.OSM_VALHALLA -> "OSM online (Valhalla)"
        RoutingBackend.OSM_GRAPHHOPPER -> "OSM online (GraphHopper)"
        RoutingBackend.OSM_OFFLINE -> "OSM offline"
    }

private const val M_PER_KM = 1000.0
private const val SECONDS_PER_MINUTE = 60L
private const val PREVIEW_STEPS = 6
