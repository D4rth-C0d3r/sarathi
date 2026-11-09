package io.github.d4rthc0d3r.sarathi.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.github.d4rthc0d3r.sarathi.R

/** Debug builds' desk tests in Maps settings: the failure drill (F8) and the map data check (G2). */
@Composable
internal fun DeskTestSections(
    status: MapsStatus,
    actions: MapsSettingsActions,
) {
    status.drill?.let { on ->
        Section(R.string.drill_title, R.string.drill_reason) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.drill_switch), Modifier.weight(1f))
                Switch(checked = on, onCheckedChange = actions.onDrill)
            }
        }
    }
    status.mapCheck?.let { result ->
        Section(R.string.map_check_title, R.string.map_check_reason) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = { actions.onMapCheck(false) }) {
                    Text(stringResource(R.string.map_check_online))
                }
                OutlinedButton(onClick = { actions.onMapCheck(true) }) {
                    Text(stringResource(R.string.map_check_offline))
                }
            }
            if (result.isNotEmpty()) Text(result, style = MaterialTheme.typography.bodySmall)
        }
    }
}
