package io.github.d4rthc0d3r.sarathi.feature.destination

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp

/** The confirmation's checks and its Start button (ADR-0008). */
@Composable
internal fun StartPanel(
    checks: StartChecks,
    actions: DestinationActions,
) {
    val small = MaterialTheme.typography.bodySmall
    checks.costLine?.let { Text(it, style = small) }
    Check(checks.dashPaired, R.string.check_paired)
    Check(checks.dashConnected, R.string.check_connected)
    Check(checks.backgroundLocation, R.string.check_background_location)
    Check(checks.batteryExempt, R.string.check_battery)
    Button(onClick = actions.onStartRide, enabled = checks.dashPaired) { Text(stringResource(R.string.start_ride)) }
    Text(stringResource(R.string.start_note), style = small)
    if (checks.canSimulate) {
        Text(stringResource(R.string.simulate_ride_note), style = small)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            SIM_SPEEDUPS.forEach { x ->
                OutlinedButton(onClick = { actions.onSimulateRide(x) }, enabled = checks.dashPaired) {
                    Text(stringResource(R.string.simulate_ride, x))
                }
            }
        }
    }
}

@Composable
private fun Check(
    ok: Boolean,
    label: Int,
) {
    val state = stringResource(if (ok) R.string.check_ok else R.string.check_not_yet)
    Text(
        "${stringResource(label)}: $state",
        style = MaterialTheme.typography.bodySmall,
        color = if (ok) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.error,
    )
}

/** Desk-test speeds: real time, and faster to see a whole ride. */
private const val REAL_TIME = 1
private const val FAST = 5
private const val FASTER = 20
private val SIM_SPEEDUPS = listOf(REAL_TIME, FAST, FASTER)
