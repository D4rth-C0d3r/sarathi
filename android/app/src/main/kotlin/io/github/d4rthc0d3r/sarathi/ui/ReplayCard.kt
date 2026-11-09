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

/** The desk-test recording and its replay (step F8, debug builds). */
@Composable
fun ReplayCard(
    replay: ReplayInfo,
    onReplay: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    Card(modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(
                stringResource(R.string.replay_title, replay.destination),
                style = MaterialTheme.typography.titleMedium,
            )
            Text(
                stringResource(R.string.replay_detail, replay.backend, replay.km),
                style = MaterialTheme.typography.bodySmall,
            )
            OutlinedButton(
                onClick = { onReplay(REPLAY_SPEEDUP) },
            ) { Text(stringResource(R.string.replay_button, REPLAY_SPEEDUP)) }
        }
    }
}

private const val REPLAY_SPEEDUP = 20
