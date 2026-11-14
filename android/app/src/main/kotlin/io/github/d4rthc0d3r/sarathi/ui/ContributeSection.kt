package io.github.d4rthc0d3r.sarathi.ui

import androidx.compose.foundation.layout.Row
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import io.github.d4rthc0d3r.sarathi.R

/**
 * The OSM contribution helper (ADR-0012): off by default. Turning it on first explains what is
 * recorded and what can be shared; turning it off deletes the tracks, after a confirmation.
 */
@Composable
internal fun ContributeSection(
    status: MapsStatus,
    onContribute: (Boolean) -> Unit,
    onOpenGaps: () -> Unit,
) {
    var asking by remember { mutableStateOf<Boolean?>(null) } // the switch position being confirmed
    Section(R.string.contribute_title, R.string.contribute_reason) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                if (status.contributeOn) {
                    stringResource(R.string.contribute_on, status.contributeRides, status.contributeKm)
                } else {
                    stringResource(R.string.contribute_off)
                },
                Modifier.weight(1f),
            )
            Switch(checked = status.contributeOn, onCheckedChange = { asking = it })
        }
        if (status.contributeOn) {
            OutlinedButton(onClick = onOpenGaps) { Text(stringResource(R.string.contribute_open_gaps)) }
        }
    }
    when (asking) {
        true -> {
            ConfirmDialog(R.string.contribute_on_title, R.string.contribute_on_text, R.string.contribute_on_confirm) {
                if (it) onContribute(true)
                asking = null
            }
        }

        false -> {
            ConfirmDialog(
                R.string.contribute_off_title,
                R.string.contribute_off_text,
                R.string.contribute_off_confirm,
            ) {
                if (it) onContribute(false)
                asking = null
            }
        }

        null -> {
            Unit
        }
    }
}

@Composable
private fun ConfirmDialog(
    title: Int,
    text: Int,
    confirm: Int,
    onResult: (Boolean) -> Unit,
) {
    AlertDialog(
        onDismissRequest = { onResult(false) },
        title = { Text(stringResource(title)) },
        text = { Text(stringResource(text)) },
        confirmButton = { TextButton(onClick = { onResult(true) }) { Text(stringResource(confirm)) } },
        dismissButton = { TextButton(onClick = { onResult(false) }) { Text(stringResource(R.string.cancel)) } },
    )
}
