package io.github.d4rthc0d3r.sarathi.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.github.d4rthc0d3r.sarathi.R
import io.github.d4rthc0d3r.sarathi.contribute.GapActions.EditPreview
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.Gap
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.Join
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.NoEdit
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.RoadType
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.StoredGap

/**
 * Reviews a guided edit before anything is sent (ADR-0012 §4): the new road from the trace over
 * the roads OSM has there now, where it joins them, the road type the rider picks, and the
 * changeset comment in the rider's words.
 */
@Composable
internal fun EditDialog(
    gap: StoredGap,
    actions: GapsActions,
    onClose: () -> Unit,
) {
    val preview by produceState<EditPreview?>(null, gap) { value = actions.prepareEdit(gap) }
    var road by remember { mutableStateOf<RoadType?>(null) }
    val draft = stringResource(R.string.edit_comment_draft)
    var comment by remember { mutableStateOf(draft) }
    val ready = preview as? EditPreview.Ready
    AlertDialog(
        onDismissRequest = onClose,
        title = { Text(stringResource(R.string.edit_title)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                when (val p = preview) {
                    null -> Text(stringResource(R.string.edit_loading))
                    is EditPreview.Refused -> Text(stringResource(R.string.edit_refused, refusal(p.why)))
                    is EditPreview.Failed -> Text(stringResource(R.string.edit_failed))
                    is EditPreview.Ready -> Review(p, road, { road = it }, comment, { comment = it.take(MAX_COMMENT) })
                }
            }
        },
        confirmButton = {
            TextButton(enabled = ready != null && road != null && comment.isNotBlank(), onClick = {
                val plan = ready?.plan
                val type = road
                if (plan != null && type != null) actions.onSendEdit(gap, plan, type, comment.trim())
                onClose()
            }) { Text(stringResource(R.string.edit_send)) }
        },
        dismissButton = { TextButton(onClick = onClose) { Text(stringResource(R.string.cancel)) } },
    )
}

@Composable
private fun Review(
    p: EditPreview.Ready,
    road: RoadType?,
    onRoad: (RoadType) -> Unit,
    comment: String,
    onComment: (String) -> Unit,
) {
    GapMap(Gap(p.plan.line, 0.0, 0, 0), { p.roads }, listOf(p.plan.start.at, p.plan.end.at))
    Text(stringResource(R.string.map_attribution), style = MaterialTheme.typography.bodySmall)
    Text(stringResource(R.string.edit_joins, joinText(p.plan.start), joinText(p.plan.end), p.plan.lengthM.toInt()))
    Text(stringResource(R.string.edit_type))
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        RoadType.entries.forEach { t ->
            FilterChip(selected = road == t, onClick = { onRoad(t) }, label = { Text(roadName(t)) })
        }
    }
    OutlinedTextField(
        value = comment,
        onValueChange = onComment,
        label = { Text(stringResource(R.string.edit_comment)) },
        modifier = Modifier.fillMaxWidth(),
    )
    Text(stringResource(R.string.edit_marks), style = MaterialTheme.typography.bodySmall)
}

@Composable
private fun joinText(join: Join): String =
    stringResource(if (join is Join.Existing) R.string.edit_join_existing else R.string.edit_join_new)

@Composable
private fun roadName(t: RoadType): String =
    stringResource(
        when (t) {
            RoadType.RESIDENTIAL -> R.string.road_residential
            RoadType.UNCLASSIFIED -> R.string.road_unclassified
            RoadType.SERVICE -> R.string.road_service
            RoadType.TRACK -> R.string.road_track
            RoadType.PATH -> R.string.road_path
        },
    )

@Composable
private fun refusal(why: NoEdit): String =
    stringResource(
        when (why) {
            NoEdit.START_NOT_NEAR_A_ROAD, NoEdit.END_NOT_NEAR_A_ROAD, NoEdit.SAME_ROAD_POINT -> R.string.edit_no_join
            NoEdit.TOO_LONG, NoEdit.TOO_MANY_NODES -> R.string.edit_too_long
        },
    )

private const val MAX_COMMENT = 255 // OSM's tag value limit
