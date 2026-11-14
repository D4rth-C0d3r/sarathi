package io.github.d4rthc0d3r.sarathi.contribute

import android.content.Context
import android.util.Log
import io.github.d4rthc0d3r.sarathi.BuildConfig
import io.github.d4rthc0d3r.sarathi.R
import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint
import io.github.d4rthc0d3r.sarathi.core.mapdata.TileShapes
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.DismissedAreas
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.Gap
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.GapFinder
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.GapStatus
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.OsmTiles
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.RoadIndex
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.StoredGap
import io.github.d4rthc0d3r.sarathi.maps.MapsRegistry
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Finds the gaps of finished rides (ADR-0012 §3), when the rider asks, never during a ride: each
 * new track against OSM map tiles, then every candidate against current OSM through Overpass.
 * Candidates Overpass couldn't check stay unchecked and are tried again next time.
 */
object GapCheck {
    private const val TAG = "Contribute"
    private const val TILE_EVERY = 10 // fixes: a tile is about 2.4 km, so every 10th fix is plenty

    /** Checks new rides and unchecked gaps; returns how many gaps wait for the rider. */
    suspend fun run(
        context: Context,
        progress: (String) -> Unit,
    ): Int =
        withContext(Dispatchers.IO) {
            val registry = MapsRegistry.get(context)
            val overpass = OverpassClient(registry.http, "sarathi/${BuildConfig.VERSION_NAME}")
            val dismissed = GapStore.dismissed(context)
            for (track in TrackStorage.finished(context).filterNot { GapStore.checked(context, it.name) }) {
                withContext(Dispatchers.Main) { progress(context.getString(R.string.gaps_checking_ride)) }
                val fixes = TrackStorage.read(track)
                val tiles =
                    tiles(
                        registry,
                        OsmTiles.around(
                            fixes
                                .filterIndexed {
                                    i,
                                    _,
                                    ->
                                    i % TILE_EVERY == 0
                                }.map { GeoPoint(it.lat, it.lon) },
                        ),
                    )
                val candidates = GapFinder.find(fixes, RoadIndex.fromTiles(tiles)).filterNot(dismissed::covers)
                Log.i(TAG, "${track.name}: ${fixes.size} fixes, ${tiles.size} tiles, ${candidates.size} candidate(s)")
                GapStore.save(context, track.name, candidates.flatMap { confirm(overpass, dismissed, track.name, it) })
            }
            withContext(Dispatchers.Main) { progress(context.getString(R.string.gaps_checking_osm)) }
            for (g in GapStore.all(context).filter { it.status == GapStatus.UNCHECKED }) {
                val now = confirm(overpass, dismissed, g.track, g.gap)
                if (now.none { it.status == GapStatus.UNCHECKED }) {
                    GapStore.replace(context, g, now)
                }
            }
            GapStore.all(context).count { it.status == GapStatus.OPEN }
        }

    /** The OSM roads near [gap], for its map. */
    suspend fun roadsNear(
        context: Context,
        gap: Gap,
    ): List<List<GeoPoint>> =
        withContext(Dispatchers.IO) {
            OsmTiles.roads(tiles(MapsRegistry.get(context), OsmTiles.around(listOf(gap.middle))))
        }

    private suspend fun tiles(
        registry: MapsRegistry,
        ids: List<io.github.d4rthc0d3r.sarathi.core.mapdata.TileId>,
    ): List<TileShapes> = ids.mapNotNull { registry.mapData.shapes(it)?.shapes }

    private suspend fun confirm(
        overpass: OverpassClient,
        dismissed: DismissedAreas,
        track: String,
        candidate: Gap,
    ): List<StoredGap> {
        val current = overpass.around(candidate) ?: return listOf(StoredGap(track, candidate, GapStatus.UNCHECKED))
        return GapFinder
            .confirm(candidate, current)
            .filterNot(dismissed::covers)
            .map { StoredGap(track, it, GapStatus.OPEN) }
    }
}
