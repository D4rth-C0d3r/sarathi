package io.github.d4rthc0d3r.sarathi.contribute

import android.content.Context
import android.util.Log
import io.github.d4rthc0d3r.sarathi.BuildConfig
import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.Contribution
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.DailyLimits
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.EditPlan
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.GapStatus
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.Gpx
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.GuidedEdit
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.NoEdit
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.OsmChange
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.OsmData
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.RawGpsFix
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.RoadType
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.StoredGap
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.TraceCut
import io.github.d4rthc0d3r.sarathi.feature.destination.FilePlacesStore
import io.github.d4rthc0d3r.sarathi.maps.MapsRegistry
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import kotlin.math.cos

/**
 * What the rider can do with a gap (ADR-0012 §4), each after confirming it: upload the GPS trace
 * around it (privacy cut applied) or add a note. Our daily limits come first.
 */
object GapActions {
    private const val TAG = "Contribute"
    private const val LIMITS = "osm-limits.txt"
    private const val EDIT_MARGIN_M = 100.0
    private const val M_PER_DEG = 111_320.0
    const val LIMIT_REACHED = "today's limit"

    /** The fixes that would be uploaded for [gap]: the stretch around it, less the privacy cut. */
    suspend fun tracePoints(
        context: Context,
        gap: StoredGap,
    ): List<RawGpsFix> =
        withContext(Dispatchers.IO) {
            val file =
                TrackStorage.finished(context).firstOrNull { it.name == gap.track } ?: return@withContext emptyList()
            // Saved places only take points away here; nothing about them is sent.
            val saved =
                FilePlacesStore(context).book.value.saved.mapNotNull { s ->
                    s.kept.place.at
                        ?.let { GeoPoint(it.lat, it.lng) }
                }
            TraceCut.forGap(TrackStorage.read(file), gap.gap, saved)
        }

    /** Whether today's limit for [kind] still allows one more. */
    fun allowed(
        context: Context,
        kind: Contribution,
    ): Boolean = limits(context).allowed(kind, System.currentTimeMillis())

    suspend fun sendTrace(
        context: Context,
        gap: StoredGap,
        description: String,
        visibility: TraceVisibility,
    ): Sent {
        val points = tracePoints(context, gap)
        if (points.size < 2) return Sent.Failed("nothing left after the privacy cut")
        return send(context, gap, Contribution.TRACE, GapStatus.TRACE_SENT) { api ->
            api.uploadTrace(Gpx.write(points, OsmApi.USER_AGENT), description, visibility)
        }
    }

    suspend fun sendNote(
        context: Context,
        gap: StoredGap,
        text: String,
    ): Sent =
        send(context, gap, Contribution.NOTE, GapStatus.NOTE_SENT) { api ->
            api.addNote(gap.gap.middle, text)
        }

    /** What a guided edit of [gap] would be, from OSM as it is now (ADR-0012 §4). */
    sealed interface EditPreview {
        data class Ready(
            val plan: EditPlan,
            /** The roads OSM has there now, for the review map. */
            val roads: List<List<GeoPoint>>,
        ) : EditPreview

        data class Refused(
            val why: NoEdit,
        ) : EditPreview

        data class Failed(
            val sent: Sent,
        ) : EditPreview
    }

    suspend fun prepareEdit(
        context: Context,
        gap: StoredGap,
    ): EditPreview {
        val token = OsmAccount.token(context) ?: return EditPreview.Failed(Sent.SignedOut)
        val data = download(context, token, gap.gap.points)
        return when (val r = data?.let { GuidedEdit.plan(gap.gap, it) }) {
            null -> EditPreview.Failed(Sent.Failed("couldn't download the area"))
            is GuidedEdit.Result.Ok -> EditPreview.Ready(r.plan, data.ways.filter { it.joinable }.map(data::line))
            is GuidedEdit.Result.Refused -> EditPreview.Refused(r.why)
        }
    }

    // OSM as it is now, in a box around the gap with a margin for the roads it joins.
    private suspend fun download(
        context: Context,
        token: String,
        points: List<GeoPoint>,
    ): OsmData? {
        val marginLat = EDIT_MARGIN_M / M_PER_DEG
        val marginLon = marginLat / cos(Math.toRadians(points.first().lat))
        return OsmApi(MapsRegistry.get(context).http, token).map(
            points.minOf { it.lat } - marginLat,
            points.minOf { it.lng } - marginLon,
            points.maxOf { it.lat } + marginLat,
            points.maxOf { it.lng } + marginLon,
        )
    }

    suspend fun sendEdit(
        context: Context,
        gap: StoredGap,
        plan: EditPlan,
        road: RoadType,
        comment: String,
    ): Sent =
        send(context, gap, Contribution.EDIT, GapStatus.EDITED) { api ->
            api.edit(
                OsmChange.changeset(comment, "sarathi ${BuildConfig.VERSION_NAME}"),
            ) { cs -> OsmChange.build(plan, road, cs) }
        }

    private suspend fun send(
        context: Context,
        gap: StoredGap,
        kind: Contribution,
        done: GapStatus,
        call: suspend (OsmApi) -> Sent,
    ): Sent {
        val token = OsmAccount.token(context)
        val result =
            when {
                token == null -> Sent.SignedOut
                !allowed(context, kind) -> Sent.Failed(LIMIT_REACHED)
                else -> call(OsmApi(MapsRegistry.get(context).http, token))
            }
        when (result) {
            is Sent.Ok -> {
                withContext(Dispatchers.IO) {
                    GapStore.update(context, gap, done, result.id.toString())
                    record(context, kind)
                }
                Log.i(TAG, "osm: ${kind.name.lowercase()} ${result.id} sent")
            }

            Sent.SignedOut -> {
                OsmAccount.forget(context)
            }

            is Sent.Failed -> {
                Unit
            }
        }
        return result
    }

    @Synchronized
    private fun limits(context: Context): DailyLimits {
        val f = File(context.filesDir, LIMITS)
        return if (f.exists()) f.useLines { DailyLimits.read(it) } else DailyLimits()
    }

    @Synchronized
    private fun record(
        context: Context,
        kind: Contribution,
    ) {
        val l = limits(context).apply { record(kind, System.currentTimeMillis()) }
        File(context.filesDir, LIMITS).writeText(l.lines().joinToString("") { it + "\n" })
    }
}
