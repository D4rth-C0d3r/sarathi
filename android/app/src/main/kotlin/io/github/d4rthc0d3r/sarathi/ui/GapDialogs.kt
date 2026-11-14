package io.github.d4rthc0d3r.sarathi.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.github.d4rthc0d3r.sarathi.R
import io.github.d4rthc0d3r.sarathi.contribute.TraceVisibility
import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.Gap
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.StoredGap
import java.text.DateFormat
import java.util.Date

/**
 * Confirms a GPS trace upload (ADR-0012 §4): the points that would go (after the privacy cut) on
 * a map, the visibility and the description. Nothing is sent until the rider taps Upload.
 */
@Composable
internal fun TraceDialog(
    gap: StoredGap,
    actions: GapsActions,
    onClose: () -> Unit,
) {
    val points by produceState<List<GeoPoint>?>(null, gap) { value = actions.tracePoints(gap) }
    var visibility by remember { mutableStateOf(TraceVisibility.IDENTIFIABLE) }
    val draft = stringResource(R.string.trace_description_draft)
    var description by remember { mutableStateOf(draft) }
    val ready = (points?.size ?: 0) >= 2 && description.isNotBlank()
    AlertDialog(
        onDismissRequest = onClose,
        title = { Text(stringResource(R.string.trace_title)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                val p = points
                when {
                    p == null -> {
                        Text(stringResource(R.string.trace_loading))
                    }

                    p.size < 2 -> {
                        Text(stringResource(R.string.trace_nothing_left))
                    }

                    else -> {
                        // The upload drawn as its own "gap": exactly the points that would go.
                        GapMap(Gap(p, 0.0, 0, 0), actions.roads)
                        Text(stringResource(R.string.trace_points, p.size))
                    }
                }
                Text(stringResource(R.string.trace_cut), style = MaterialTheme.typography.bodySmall)
                VisibilityChoice(visibility) { visibility = it }
                OutlinedTextField(
                    value = description,
                    onValueChange = { description = it.take(MAX_DESCRIPTION) },
                    label = { Text(stringResource(R.string.trace_description)) },
                    modifier = Modifier.fillMaxWidth(),
                )
                Text(stringResource(R.string.osm_public_forever), style = MaterialTheme.typography.bodySmall)
            }
        },
        confirmButton = {
            TextButton(enabled = ready, onClick = {
                actions.onSendTrace(gap, description.trim(), visibility)
                onClose()
            }) { Text(stringResource(R.string.trace_upload)) }
        },
        dismissButton = { TextButton(onClick = onClose) { Text(stringResource(R.string.cancel)) } },
    )
}

@Composable
private fun VisibilityChoice(
    selected: TraceVisibility,
    onSelect: (TraceVisibility) -> Unit,
) {
    TraceVisibility.entries.forEach { v ->
        Row(
            Modifier.fillMaxWidth().selectable(selected = selected == v, onClick = { onSelect(v) }),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            RadioButton(selected = selected == v, onClick = { onSelect(v) })
            Text(
                stringResource(
                    if (v ==
                        TraceVisibility.IDENTIFIABLE
                    ) {
                        R.string.trace_identifiable
                    } else {
                        R.string.trace_trackable
                    },
                ),
            )
        }
    }
}

/**
 * Confirms an OSM note (ADR-0012 §4): the rider's words, from a short draft; sent only when the
 * rider taps Send. Notes are for people, so nothing else is added to them.
 */
@Composable
internal fun NoteDialog(
    gap: StoredGap,
    actions: GapsActions,
    onClose: () -> Unit,
) {
    val date = DateFormat.getDateInstance(DateFormat.MEDIUM).format(Date(gap.gap.startUtcMs))
    val draft = stringResource(R.string.note_draft, gap.gap.lengthM.toInt(), date)
    var text by remember { mutableStateOf(draft) }
    AlertDialog(
        onDismissRequest = onClose,
        title = { Text(stringResource(R.string.note_title)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(
                    value = text,
                    onValueChange = { text = it.take(MAX_NOTE) },
                    modifier = Modifier.fillMaxWidth(),
                )
                Text(stringResource(R.string.note_public), style = MaterialTheme.typography.bodySmall)
            }
        },
        confirmButton = {
            TextButton(enabled = text.isNotBlank(), onClick = {
                actions.onSendNote(gap, text.trim())
                onClose()
            }) { Text(stringResource(R.string.note_send)) }
        },
        dismissButton = { TextButton(onClick = onClose) { Text(stringResource(R.string.cancel)) } },
    )
}

private const val MAX_DESCRIPTION = 255 // OSM's limit
private const val MAX_NOTE = 2000
