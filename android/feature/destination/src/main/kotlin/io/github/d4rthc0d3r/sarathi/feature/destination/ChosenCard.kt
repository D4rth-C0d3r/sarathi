package io.github.d4rthc0d3r.sarathi.feature.destination

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.github.d4rthc0d3r.sarathi.core.maps.Label
import io.github.d4rthc0d3r.sarathi.core.maps.Place
import java.util.Locale

/** The chosen destination, with saving it; routing it comes with steps F4 and F7. */
@Composable
internal fun ChosenCard(
    place: Place,
    actions: DestinationActions,
) {
    var favourite by rememberSaveable(place) { mutableStateOf<String?>(null) }
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(stringResource(R.string.dest_chosen), style = MaterialTheme.typography.labelLarge)
            Text(place.name, style = MaterialTheme.typography.titleLarge)
            if (place.address.isNotBlank()) Text(place.address)
            place.at?.let {
                Text(
                    String.format(Locale.ROOT, "%.5f, %.5f", it.lat, it.lng),
                    style = MaterialTheme.typography.bodySmall,
                )
            }
            Text(
                stringResource(R.string.dest_from, sourceName(place.source)),
                style = MaterialTheme.typography.bodySmall,
            )
            OutlinedButton(
                onClick = { actions.onSave(Label.HOME, "Home") },
            ) { Text(stringResource(R.string.dest_save_home)) }
            OutlinedButton(
                onClick = { actions.onSave(Label.WORK, "Work") },
            ) { Text(stringResource(R.string.dest_save_work)) }
            val name = favourite
            if (name == null) {
                OutlinedButton(
                    onClick = { favourite = place.name },
                ) { Text(stringResource(R.string.dest_save_favourite)) }
            } else {
                OutlinedTextField(
                    value = name,
                    onValueChange = { favourite = it },
                    label = { Text(stringResource(R.string.dest_favourite_name)) },
                    singleLine = true,
                )
                TextButton(enabled = name.isNotBlank(), onClick = {
                    actions.onSave(Label.FAVOURITE, name)
                    favourite = null
                }) { Text(stringResource(R.string.dest_save)) }
            }
            OutlinedButton(
                onClick = { actions.onRoute(null, listOf(place)) },
            ) { Text(stringResource(R.string.route_button)) }
            TextButton(onClick = actions.onClearChosen) { Text(stringResource(R.string.dest_clear)) }
        }
    }
}
