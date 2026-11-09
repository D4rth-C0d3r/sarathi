package io.github.d4rthc0d3r.sarathi.ui

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
import io.github.d4rthc0d3r.sarathi.R
import io.github.d4rthc0d3r.sarathi.core.maps.GuidanceState
import java.util.Locale

/** The ride in progress and how to end it (step F7). */
@Composable
fun RideCard(
    ride: RideStatus,
    onEnd: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Card(modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(stringResource(R.string.ride_to, ride.destination), style = MaterialTheme.typography.titleMedium)
            val g = ride.guidance
            if (g == null) {
                Text(stringResource(R.string.ride_waiting), style = MaterialTheme.typography.bodySmall)
            } else {
                val what =
                    g.maneuver.name
                        .lowercase(Locale.ROOT)
                        .replace('_', ' ')
                Text(stringResource(R.string.ride_next, what, g.distanceToManeuverM.toInt()))
                Text(
                    stringResource(R.string.ride_left, g.remainingM / M_PER_KM, g.remainingS / SECONDS_PER_MINUTE),
                    style = MaterialTheme.typography.bodySmall,
                )
            }
            OutlinedButton(onClick = onEnd) { Text(stringResource(R.string.ride_end)) }
        }
    }
}

private const val M_PER_KM = 1000.0
private const val SECONDS_PER_MINUTE = 60L
