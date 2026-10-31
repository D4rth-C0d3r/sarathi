package io.github.d4rthc0d3r.sarathi.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import io.github.d4rthc0d3r.sarathi.R
import io.github.d4rthc0d3r.sarathi.core.ble.LinkState
import io.github.d4rthc0d3r.sarathi.core.protocol.Lock
import io.github.d4rthc0d3r.sarathi.link.LinkRepository
import io.github.d4rthc0d3r.sarathi.link.LinkText

/** Everything the rider does before a ride, on one screen: setup, then the ride link. */
@Composable
fun HomeScreen(
    setup: SetupState,
    link: LinkRepository.Status,
    lock: Lock,
    actions: HomeActions,
    modifier: Modifier = Modifier,
) {
    Scaffold(modifier = modifier) { padding ->
        Column(
            modifier =
                Modifier
                    .padding(padding)
                    .padding(16.dp)
                    .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(stringResource(R.string.app_name), style = MaterialTheme.typography.headlineMedium)
            setup.next?.let { SetupCard(it, actions.onSetupStep) }
            if (setup.dashAddress != null) {
                LinkCard(setup, link, actions)
                LockCard(lock, actions.onLock)
            }
            if (SetupStep.BATTERY in setup.done) BatteryCard()
            OutlinedButton(onClick = actions.onMapsSettings) { Text(stringResource(R.string.maps_settings)) }
            if (setup.dashAddress != null) {
                OutlinedButton(onClick = actions.onForget) { Text(stringResource(R.string.forget_dash)) }
            }
        }
    }
}

@Composable
private fun SetupCard(
    step: SetupStep,
    onSetupStep: (SetupStep) -> Unit,
) {
    val (title, reason, action) = SetupTexts.of(step)
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(stringResource(title), style = MaterialTheme.typography.titleMedium)
            Text(stringResource(reason))
            Button(onClick = { onSetupStep(step) }) { Text(stringResource(action)) }
        }
    }
}

@Composable
private fun LinkCard(
    setup: SetupState,
    link: LinkRepository.Status,
    actions: HomeActions,
) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(stringResource(R.string.ride_link), style = MaterialTheme.typography.titleMedium)
            Text(stringResource(LinkText.of(link.link)))
            (link.link as? LinkState.Ready)?.let { Text(stringResource(R.string.dash_firmware, it.dash.fw)) }
            if (link.running) {
                Text(
                    if (link.locationActive) {
                        pluralStringResource(R.plurals.location_fixes, link.locationFixes, link.locationFixes)
                    } else {
                        stringResource(R.string.location_off)
                    },
                )
                OutlinedButton(onClick = actions.onStopLink) { Text(stringResource(R.string.stop_link)) }
            } else {
                Button(onClick = actions.onStartLink) { Text(stringResource(R.string.start_link)) }
            }
            if (setup.autoStartSupported) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    Column(Modifier.weight(1f)) {
                        Text(stringResource(R.string.auto_start))
                        Text(stringResource(R.string.auto_start_reason), style = MaterialTheme.typography.bodySmall)
                    }
                    Switch(checked = setup.autoStart, onCheckedChange = actions.onAutoStart)
                }
            }
        }
    }
}

@Composable
private fun BatteryCard() {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(stringResource(R.string.battery_title), style = MaterialTheme.typography.titleMedium)
            Text(stringResource(BatteryGuidance.forThisPhone()))
            Text(stringResource(R.string.battery_more), style = MaterialTheme.typography.bodySmall)
        }
    }
}

@Preview
@Composable
private fun HomeScreenSetupPreview() {
    HomeScreen(SetupState(done = setOf(SetupStep.NEARBY)), LinkRepository.Status(), Lock(), HomeActions())
}

@Preview
@Composable
private fun HomeScreenReadyPreview() {
    HomeScreen(
        SetupState(done = SetupStep.entries.toSet(), dashAddress = "44:BD:8D:71:CA:F9", autoStartSupported = true),
        LinkRepository.Status(running = true, link = LinkState.Waiting, locationActive = true, locationFixes = 42),
        Lock(),
        HomeActions(),
    )
}
