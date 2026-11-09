package io.github.d4rthc0d3r.sarathi.feature.destination

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.github.d4rthc0d3r.sarathi.core.links.SharedMode

/** A route shared from another app: its stops in order and the travel mode (ADR-0008). */
@Composable
internal fun TripCard(
    trip: SharedTrip,
    actions: DestinationActions,
) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(stringResource(R.string.dest_shared_route), style = MaterialTheme.typography.labelLarge)
            val origin = trip.origin
            Text("1. " + (origin?.name ?: stringResource(R.string.dest_your_location)))
            trip.via.forEachIndexed { i, stop -> Text("${i + 2}. ${stop.name}") }
            Text("${trip.via.size + 2}. ${trip.destination.name}", style = MaterialTheme.typography.titleMedium)
            val note =
                when {
                    trip.unsupportedMode -> R.string.dest_mode_not_for_riding
                    trip.shared == SharedMode.DRIVING -> R.string.dest_mode_car
                    else -> R.string.dest_mode_two_wheeler
                }
            Text(stringResource(note), style = MaterialTheme.typography.bodySmall)
            Text(stringResource(R.string.dest_routes_later), style = MaterialTheme.typography.bodySmall)
            OutlinedButton(onClick = { actions.onRoute(trip.origin, trip.via + trip.destination) }) {
                Text(stringResource(R.string.route_button))
            }
            TextButton(onClick = actions.onClearChosen) { Text(stringResource(R.string.dest_clear)) }
        }
    }
}

@Composable
internal fun linkProblemText(e: LinkException): String {
    val problem =
        stringResource(
            when (e.problem) {
                LinkProblem.NOT_SUPPORTED -> R.string.link_not_supported
                LinkProblem.NOT_RESOLVED -> R.string.link_not_resolved
                LinkProblem.NOT_FOUND -> R.string.link_not_found
            },
        )
    return e.reason?.let { "$problem (${reasonText(it)})" } ?: problem
}
