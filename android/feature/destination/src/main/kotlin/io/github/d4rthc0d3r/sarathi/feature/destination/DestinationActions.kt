package io.github.d4rthc0d3r.sarathi.feature.destination

import io.github.d4rthc0d3r.sarathi.core.maps.Label
import io.github.d4rthc0d3r.sarathi.core.maps.Place
import io.github.d4rthc0d3r.sarathi.core.maps.SavedPlace
import io.github.d4rthc0d3r.sarathi.core.maps.Suggestion

/** What the search screen can do; the view model's functions. */
data class DestinationActions(
    val onQuery: (String) -> Unit = {},
    val onSuggestion: (Suggestion) -> Unit = {},
    val onKept: (Place) -> Unit = {},
    val onSave: (Label, String) -> Unit = { _, _ -> },
    val onRemoveRecent: (Place) -> Unit = {},
    val onRemoveSaved: (SavedPlace) -> Unit = {},
    val onClearChosen: () -> Unit = {},
    /** Route from the origin (null: here) through the stops, at the desk (step F4). */
    val onRoute: (origin: Place?, stops: List<Place>) -> Unit = { _, _ -> },
    /** Rides the routed result in the simulator, with or without a detour (step F6). */
    val onSimulate: (detour: Boolean) -> Unit = {},
    val onStopSimulation: () -> Unit = {},
    val onBack: () -> Unit = {},
)
