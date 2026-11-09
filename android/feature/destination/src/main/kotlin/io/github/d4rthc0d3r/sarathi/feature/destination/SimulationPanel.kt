package io.github.d4rthc0d3r.sarathi.feature.destination

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import io.github.d4rthc0d3r.sarathi.core.maps.GuidanceState
import io.github.d4rthc0d3r.sarathi.core.maps.StepManeuver
import java.util.Locale

/** The simulated ride's live guidance, for the desk (step F6). */
@Composable
internal fun SimulationPanel(
    sim: Simulation?,
    actions: DestinationActions,
) {
    val small = MaterialTheme.typography.bodySmall
    if (sim?.running == true) {
        OutlinedButton(onClick = actions.onStopSimulation) { Text(stringResource(R.string.sim_stop)) }
    } else {
        OutlinedButton(onClick = { actions.onSimulate(false) }) { Text(stringResource(R.string.sim_ride)) }
        OutlinedButton(onClick = { actions.onSimulate(true) }) { Text(stringResource(R.string.sim_detour)) }
    }
    when {
        sim == null -> Unit
        sim.unsupported -> Text(stringResource(R.string.sim_unsupported), style = small)
        else -> sim.guidance?.let { GuidanceLines(it, sim) }
    }
}

@Composable
private fun GuidanceLines(
    g: GuidanceState,
    sim: Simulation,
) {
    val small = MaterialTheme.typography.bodySmall
    val what =
        g.maneuver.name
            .lowercase(Locale.ROOT)
            .replace('_', ' ') + if (g.exit > 0) " (exit ${g.exit})" else ""
    val road =
        if (g.roadName.isNotBlank() && !g.arrived &&
            g.maneuver != StepManeuver.ARRIVE
        ) {
            " onto ${g.roadName}"
        } else {
            ""
        }
    Text(
        stringResource(R.string.sim_next, what, road, g.distanceToManeuverM.toInt()),
        style = MaterialTheme.typography.titleMedium,
    )
    Text(
        stringResource(
            R.string.sim_left,
            g.remainingM / M_PER_KM,
            g.remainingS / SECONDS_PER_MINUTE,
            g.stepIndex,
            sim.steps,
        ),
        style = small,
    )
    val flags =
        listOfNotNull(
            "off route".takeIf { g.offRoute },
            "rerouting".takeIf { g.rerouting },
            "arrived".takeIf { g.arrived },
        )
    if (flags.isNotEmpty()) Text(flags.joinToString(" · "), color = MaterialTheme.colorScheme.error)
    Text(stringResource(R.string.sim_reroutes, sim.reroutes), style = small)
    if (sim.lastReroute.isNotBlank()) Text(sim.lastReroute, style = small)
}

private const val M_PER_KM = 1000.0
private const val SECONDS_PER_MINUTE = 60L
