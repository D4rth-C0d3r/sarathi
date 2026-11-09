package io.github.d4rthc0d3r.sarathi.feature.destination

import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource

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
