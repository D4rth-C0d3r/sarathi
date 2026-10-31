package io.github.d4rthc0d3r.sarathi.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Card
import androidx.compose.material3.FilterChip
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
import io.github.d4rthc0d3r.sarathi.R
import io.github.d4rthc0d3r.sarathi.core.maps.GuardLevel
import io.github.d4rthc0d3r.sarathi.core.maps.MapsSettings
import io.github.d4rthc0d3r.sarathi.core.maps.MeterStatus
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend
import io.github.d4rthc0d3r.sarathi.core.maps.SearchBackend

/** Maps settings (ADR-0006): the backend, advanced overrides and fallback order, keys and usage. */
@Composable
fun MapsSettingsScreen(
    settings: MapsSettings,
    status: MapsStatus,
    onChange: ((MapsSettings) -> MapsSettings) -> Unit,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Scaffold(modifier = modifier) { padding ->
        Column(
            Modifier
                .padding(padding)
                .padding(16.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            TextButton(onClick = onBack) { Text(stringResource(R.string.back)) }
            Text(stringResource(R.string.maps_title), style = MaterialTheme.typography.headlineSmall)
            Section(R.string.maps_backend, R.string.maps_backend_reason) {
                Choices(
                    SearchBackend.entries,
                    settings.backend,
                    ::searchName,
                ) { b -> onChange { it.copy(backend = b) } }
            }
            Advanced(settings, onChange)
            Section(R.string.maps_keys, R.string.maps_keys_reason) {
                KeyRow("Google", status.googleKey)
                KeyRow("Mappls", status.mapplsConfigured)
                KeyRow("GraphHopper", status.graphHopperKey)
            }
            Section(R.string.maps_usage, R.string.maps_usage_reason) {
                status.meters.forEach { Text(meterLine(it)) }
                Stepper(
                    stringResource(R.string.maps_mappls_search_cap),
                    settings.mapplsSearchPerDay,
                    CAP_RANGE,
                    CAP_STEP,
                ) { v ->
                    onChange { it.copy(mapplsSearchPerDay = v) }
                }
                Stepper(
                    stringResource(R.string.maps_mappls_route_cap),
                    settings.mapplsRoutesPerDay,
                    CAP_RANGE,
                    CAP_STEP,
                ) { v ->
                    onChange { it.copy(mapplsRoutesPerDay = v) }
                }
            }
            Section(R.string.maps_valhalla, R.string.maps_valhalla_reason) {
                OutlinedTextField(
                    value = settings.valhallaUrl,
                    onValueChange = { url -> onChange { it.copy(valhallaUrl = url.trim()) } },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        }
    }
}

@Composable
private fun Advanced(
    settings: MapsSettings,
    onChange: ((MapsSettings) -> MapsSettings) -> Unit,
) {
    Section(R.string.maps_advanced, R.string.maps_advanced_reason) {
        Text(stringResource(R.string.maps_search_override))
        Choices(listOf(null) + SearchBackend.entries, settings.searchOverride, { searchName(it) }) { b ->
            onChange { it.copy(searchOverride = b) }
        }
        Text(stringResource(R.string.maps_routing_override))
        Choices(listOf(null) + RoutingBackend.entries, settings.routingOverride, { routingName(it) }) { b ->
            onChange { it.copy(routingOverride = b) }
        }
        Text(stringResource(R.string.maps_fallback))
        settings.fallback.forEach { b ->
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(routingName(b), Modifier.weight(1f))
                TextButton(onClick = { onChange { it.move(b, -1) } }) { Text("↑") }
                TextButton(onClick = { onChange { it.move(b, 1) } }) { Text("↓") }
            }
        }
        Text(routingName(RoutingBackend.OSM_OFFLINE) + " · " + stringResource(R.string.maps_always_last))
    }
}

private const val CAP_MAX = 10_000
private const val CAP_STEP = 10
private val CAP_RANGE = 0..CAP_MAX

@Composable
private fun Section(
    title: Int,
    reason: Int,
    content: @Composable () -> Unit,
) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(stringResource(title), style = MaterialTheme.typography.titleMedium)
            Text(stringResource(reason), style = MaterialTheme.typography.bodySmall)
            content()
        }
    }
}

@Composable
private fun <T> Choices(
    options: List<T>,
    selected: T,
    name: @Composable (T) -> String,
    onSelect: (T) -> Unit,
) {
    Column {
        options.forEach { option ->
            FilterChip(selected = option == selected, onClick = { onSelect(option) }, label = { Text(name(option)) })
        }
    }
}

@Composable
private fun KeyRow(
    name: String,
    present: Boolean,
) {
    Text("$name: " + stringResource(if (present) R.string.key_present else R.string.key_missing))
}

@Composable
private fun searchName(backend: SearchBackend?): String =
    when (backend) {
        null -> stringResource(R.string.maps_same_as_backend)
        SearchBackend.GOOGLE -> "Google"
        SearchBackend.MAPPLS -> "Mappls"
        SearchBackend.OSM -> "OpenStreetMap"
    }

@Composable
private fun routingName(backend: RoutingBackend?): String =
    when (backend) {
        null -> stringResource(R.string.maps_same_as_backend)
        RoutingBackend.GOOGLE -> "Google"
        RoutingBackend.MAPPLS -> "Mappls"
        RoutingBackend.OSM_VALHALLA -> "OSM online (Valhalla)"
        RoutingBackend.OSM_GRAPHHOPPER -> "OSM online (GraphHopper)"
        RoutingBackend.OSM_OFFLINE -> "OSM offline"
    }

@Composable
private fun meterLine(m: MeterStatus): String {
    val level =
        when (m.level) {
            GuardLevel.OK -> ""
            GuardLevel.WARN -> " · " + stringResource(R.string.meter_warn)
            GuardLevel.BLOCKED -> " · " + stringResource(R.string.meter_blocked)
        }
    return stringResource(MeterNames.of(m.meter), m.used, m.cap) + level
}

@Preview
@Composable
private fun MapsSettingsPreview() {
    MapsSettingsScreen(MapsSettings(), MapsStatus(googleKey = true), {}, {})
}
