package io.github.d4rthc0d3r.sarathi.feature.destination

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import io.github.d4rthc0d3r.sarathi.core.maps.Place
import io.github.d4rthc0d3r.sarathi.core.maps.PlaceSource
import io.github.d4rthc0d3r.sarathi.core.maps.PlacesBook
import io.github.d4rthc0d3r.sarathi.core.maps.SearchBackend
import io.github.d4rthc0d3r.sarathi.core.maps.Suggestion

/** Destination search (ADR-0008): autocomplete, then recents and saved places when not typing. */
@Composable
fun DestinationScreen(
    ui: DestinationUi,
    actions: DestinationActions,
    modifier: Modifier = Modifier,
    route: RouteUi = RouteUi(),
) {
    Scaffold(modifier = modifier) { padding ->
        Column(Modifier.padding(padding).padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                TextButton(onClick = actions.onBack) { Text(stringResource(R.string.dest_back)) }
                Text(stringResource(R.string.dest_title), style = MaterialTheme.typography.headlineSmall)
            }
            OutlinedTextField(
                value = ui.query,
                onValueChange = actions.onQuery,
                placeholder = { Text(stringResource(R.string.dest_search_hint)) },
                singleLine = true,
                trailingIcon = {
                    if (ui.query.isNotEmpty()) {
                        TextButton(onClick = { actions.onQuery("") }) { Text(stringResource(R.string.dest_clear)) }
                    }
                },
                modifier = Modifier.fillMaxWidth(),
            )
            Status(ui)
            val chosen = ui.chosen
            when {
                chosen != null || ui.trip != null -> {
                    Column(
                        Modifier.verticalScroll(rememberScrollState()),
                        verticalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        if (chosen != null) ChosenCard(chosen, actions) else ui.trip?.let { TripCard(it, actions) }
                        RoutePreview(route)
                    }
                }

                ui.query.trim().length >= DestinationViewModel.MIN_QUERY -> {
                    Suggestions(ui, actions)
                }

                else -> {
                    Book(ui.book, actions)
                }
            }
        }
    }
}

@Composable
private fun Status(ui: DestinationUi) {
    val small = MaterialTheme.typography.bodySmall
    when {
        ui.resolving -> {
            Text(stringResource(R.string.dest_looking_up), style = small)
        }

        ui.searching -> {
            Text(stringResource(R.string.dest_searching), style = small)
        }

        ui.linkProblem != null -> {
            Text(linkProblemText(ui.linkProblem), color = MaterialTheme.colorScheme.error)
        }

        ui.failure != null -> {
            Text(
                stringResource(R.string.dest_failed, reasonText(ui.failure)),
                color = MaterialTheme.colorScheme.error,
            )
        }
    }
    if (ui.chosen == null && ui.backend != null) {
        Text(stringResource(R.string.dest_searched_with, backendName(ui.backend)), style = small)
        ui.skipped.forEach {
            Text(stringResource(R.string.dest_skipped, backendName(it.backend), reasonText(it.reason)), style = small)
        }
    }
}

@Composable
private fun Suggestions(
    ui: DestinationUi,
    actions: DestinationActions,
) {
    if (!ui.searching && ui.backend != null && ui.suggestions.isEmpty()) {
        Text(stringResource(R.string.dest_no_results))
    }
    LazyColumn {
        items(ui.suggestions, key = { "${it.backend}:${it.id}" }) { s ->
            PlaceRow(s.title, s.subtitle, Modifier.clickable { actions.onSuggestion(s) })
        }
    }
}

@Composable
private fun Book(
    book: PlacesBook,
    actions: DestinationActions,
) {
    if (book.saved.isEmpty() && book.recents.isEmpty()) {
        Text(stringResource(R.string.dest_empty), style = MaterialTheme.typography.bodyMedium)
        return
    }
    LazyColumn {
        if (book.saved.isNotEmpty()) header(R.string.dest_saved)
        items(book.saved, key = { "saved:${it.label}:${it.title}" }) { saved ->
            KeptRow(
                savedTitle(saved),
                saved.kept.place,
                { actions.onKept(saved.kept.place) },
            ) { actions.onRemoveSaved(saved) }
        }
        if (book.recents.isNotEmpty()) header(R.string.dest_recents)
        items(
            book.recents,
            key = { "recent:${it.place.source}:${it.place.id}:${it.place.name}:${it.place.at}" },
        ) { kept ->
            KeptRow(kept.place.name, kept.place, { actions.onKept(kept.place) }) { actions.onRemoveRecent(kept.place) }
        }
    }
}

private fun LazyListScope.header(title: Int) =
    item(key = "header:$title") {
        Text(
            stringResource(title),
            style = MaterialTheme.typography.titleMedium,
            modifier = Modifier.padding(top = 12.dp),
        )
    }

@Composable
private fun KeptRow(
    title: String,
    place: Place,
    onClick: () -> Unit,
    onRemove: () -> Unit,
) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        val subtitle =
            if (PlacesBook.needsLookup(
                    place,
                )
            ) {
                "${place.address} · ${stringResource(R.string.dest_needs_lookup)}"
            } else {
                place.address
            }
        PlaceRow(title, subtitle, Modifier.weight(1f).clickable(onClick = onClick))
        TextButton(onClick = onRemove) { Text(stringResource(R.string.dest_remove)) }
    }
}

@Composable
private fun PlaceRow(
    title: String,
    subtitle: String,
    modifier: Modifier = Modifier,
) {
    Column(modifier.fillMaxWidth().padding(vertical = 10.dp)) {
        Text(title, style = MaterialTheme.typography.bodyLarge)
        if (subtitle.isNotBlank()) Text(subtitle, style = MaterialTheme.typography.bodySmall)
    }
}

@Preview
@Composable
private fun DestinationPreview() {
    val place = Place("Basilica of Bom Jesus", "Old Goa, Goa", null, PlaceSource.OSM, "R3569663")
    DestinationScreen(
        DestinationUi(
            query = "bom jesus",
            backend = SearchBackend.OSM,
            suggestions = listOf(Suggestion(place.name, place.address, SearchBackend.OSM, "R3569663", place.at)),
        ),
        DestinationActions(),
    )
}
