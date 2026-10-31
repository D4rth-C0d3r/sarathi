package io.github.d4rthc0d3r.sarathi.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
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
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import io.github.d4rthc0d3r.sarathi.R
import io.github.d4rthc0d3r.sarathi.core.protocol.Lock

/**
 * The dash's settings lock (ADR-0011): on by default, thresholds adjustable, and a warning
 * before turning it off. Stopped stays below moving: each stepper only offers values that keep
 * it so.
 */
@Composable
fun LockCard(
    lock: Lock,
    onLock: (Lock) -> Unit,
    modifier: Modifier = Modifier,
) {
    var warn by remember { mutableStateOf(false) }
    Card(modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Column(Modifier.weight(1f)) {
                    Text(stringResource(R.string.lock_title), style = MaterialTheme.typography.titleMedium)
                    Text(stringResource(R.string.lock_reason), style = MaterialTheme.typography.bodySmall)
                }
                Switch(
                    checked = lock.enabled,
                    onCheckedChange = { if (it) onLock(lock.copy(enabled = true)) else warn = true },
                )
            }
            if (lock.enabled) {
                Stepper(stringResource(R.string.lock_move_kmh), lock.moveKmh, lock.stopKmh + 1..Lock.MOVE_KMH.last) {
                    onLock(lock.copy(moveKmh = it))
                }
                Stepper(stringResource(R.string.lock_move_s), lock.moveS, Lock.MOVE_S) { onLock(lock.copy(moveS = it)) }
                Stepper(stringResource(R.string.lock_stop_kmh), lock.stopKmh, Lock.STOP_KMH.first until lock.moveKmh) {
                    onLock(lock.copy(stopKmh = it))
                }
                Stepper(stringResource(R.string.lock_stop_s), lock.stopS, Lock.STOP_S) { onLock(lock.copy(stopS = it)) }
                // The two speeds are neighbours: neither can pass the other, so say why.
                if (lock.moveKmh - lock.stopKmh == 1) {
                    Text(stringResource(R.string.lock_hint_order), style = MaterialTheme.typography.bodySmall)
                }
            } else {
                Text(stringResource(R.string.lock_off_note), color = MaterialTheme.colorScheme.error)
            }
        }
    }
    if (warn) {
        AlertDialog(
            onDismissRequest = { warn = false },
            title = { Text(stringResource(R.string.lock_warning_title)) },
            text = { Text(stringResource(R.string.lock_warning)) },
            confirmButton = {
                TextButton(onClick = {
                    warn = false
                    onLock(lock.copy(enabled = false))
                }) { Text(stringResource(R.string.lock_turn_off)) }
            },
            dismissButton = { TextButton(onClick = { warn = false }) { Text(stringResource(R.string.lock_keep_on)) } },
        )
    }
}

@Composable
private fun Stepper(
    label: String,
    value: Int,
    range: IntRange,
    onChange: (Int) -> Unit,
) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(label, Modifier.weight(1f))
        TextButton(onClick = { onChange(value - 1) }, enabled = value - 1 in range) { Text("−") }
        Text(value.toString())
        TextButton(onClick = { onChange(value + 1) }, enabled = value + 1 in range) { Text("+") }
    }
}

@Preview
@Composable
private fun LockCardPreview() {
    LockCard(Lock(), {})
}
