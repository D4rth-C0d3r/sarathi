package io.github.d4rthc0d3r.sarathi.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import io.github.d4rthc0d3r.sarathi.R
import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.Gap
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.StoredGap
import java.text.DateFormat
import java.util.Date
import kotlin.math.cos

/** What the missing-roads screen shows (ADR-0012 §3). */
data class GapsUi(
    /** Confirmed gaps waiting for the rider. */
    val open: List<StoredGap> = emptyList(),
    /** Candidates Overpass couldn't check yet. */
    val unchecked: Int = 0,
    /** A ride is on: nothing here until it ends. */
    val riding: Boolean = false,
    /** What the check is doing now, or null when it isn't running. */
    val checking: String? = null,
)

data class GapsActions(
    val onBack: () -> Unit = {},
    val onCheck: () -> Unit = {},
    val onSkip: (StoredGap) -> Unit = {},
    val onDismiss: (StoredGap) -> Unit = {},
    /** OSM roads near a gap, for its map. */
    val roads: suspend (Gap) -> List<List<GeoPoint>> = { emptyList() },
)

/**
 * Roads you rode that OpenStreetMap doesn't have (ADR-0012): after the ride, never during it. Each
 * gap on a small map, north up; skip it, or dismiss the place for good.
 */
@Composable
fun GapsScreen(
    ui: GapsUi,
    actions: GapsActions,
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
            TextButton(onClick = actions.onBack) { Text(stringResource(R.string.back)) }
            Text(stringResource(R.string.gaps_title), style = MaterialTheme.typography.headlineSmall)
            if (ui.riding) {
                Text(stringResource(R.string.gaps_after_ride))
                return@Column
            }
            Text(stringResource(R.string.gaps_reason), style = MaterialTheme.typography.bodySmall)
            Button(onClick = actions.onCheck, enabled = ui.checking == null) {
                Text(stringResource(R.string.gaps_check))
            }
            ui.checking?.let { Text(it) }
            if (ui.unchecked > 0) Text(stringResource(R.string.gaps_unchecked, ui.unchecked))
            if (ui.open.isEmpty() && ui.checking == null) Text(stringResource(R.string.gaps_none))
            ui.open.forEach { GapCard(it, actions) }
        }
    }
}

@Composable
private fun GapCard(
    stored: StoredGap,
    actions: GapsActions,
) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            GapMap(stored.gap, actions.roads)
            Text(stringResource(R.string.map_attribution), style = MaterialTheme.typography.bodySmall)
            val ridden =
                DateFormat
                    .getDateTimeInstance(
                        DateFormat.MEDIUM,
                        DateFormat.SHORT,
                    ).format(Date(stored.gap.startUtcMs))
            Text(stringResource(R.string.gaps_card, stored.gap.lengthM.toInt(), ridden))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = { actions.onSkip(stored) }) { Text(stringResource(R.string.gaps_skip)) }
                OutlinedButton(onClick = { actions.onDismiss(stored) }) { Text(stringResource(R.string.gaps_dismiss)) }
            }
        }
    }
}

/** The gap (thick) over the OSM roads around it, north up. */
@Composable
private fun GapMap(
    gap: Gap,
    roadsFor: suspend (Gap) -> List<List<GeoPoint>>,
) {
    val roads by produceState(emptyList<List<GeoPoint>>(), gap) { value = roadsFor(gap) }
    val roadColor = MaterialTheme.colorScheme.outline
    val gapColor = MaterialTheme.colorScheme.error
    Canvas(
        Modifier
            .fillMaxWidth()
            .height(MAP_HEIGHT_DP.dp)
            .clipToBounds()
            .background(MaterialTheme.colorScheme.surface),
    ) {
        val c = gap.middle
        val k = cos(Math.toRadians(c.lat)) * M_PER_DEG
        val halfM = gap.points.maxOf { metres(it, c, k).getDistance() } + MARGIN_M
        val scale = minOf(size.width, size.height) / 2f / halfM.toFloat()
        val centre = Offset(size.width / 2f, size.height / 2f)

        fun at(p: GeoPoint) = centre + metres(p, c, k) * scale
        for (road in roads) {
            road.zipWithNext().forEach { (a, b) -> drawLine(roadColor, at(a), at(b), ROAD_PX, StrokeCap.Round) }
        }
        gap.points.zipWithNext().forEach { (a, b) -> drawLine(gapColor, at(a), at(b), GAP_PX, StrokeCap.Round) }
    }
}

// Metres east and south of the centre (screen y grows downwards).
private fun metres(
    p: GeoPoint,
    c: GeoPoint,
    k: Double,
) = Offset(((p.lng - c.lng) * k).toFloat(), (-(p.lat - c.lat) * M_PER_DEG).toFloat())

private const val M_PER_DEG = 111_320.0
private const val MARGIN_M = 150.0
private const val MAP_HEIGHT_DP = 220
private const val ROAD_PX = 4f
private const val GAP_PX = 10f

@Preview
@Composable
private fun GapsScreenPreview() {
    GapsScreen(GapsUi(unchecked = 1), GapsActions())
}
