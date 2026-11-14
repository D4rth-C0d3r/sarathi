package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint
import kotlin.math.cos

/** The road types the rider can pick for a new road (ADR-0012 §4). */
enum class RoadType(
    val highway: String,
) {
    RESIDENTIAL("residential"),
    UNCLASSIFIED("unclassified"),
    SERVICE("service"),
    TRACK("track"),
    PATH("path"),
}

/** Where an end of the new road meets an existing one. */
sealed interface Join {
    val at: GeoPoint

    /** On a node the road already has. */
    data class Existing(
        val node: OsmData.Node,
    ) : Join {
        override val at get() = node.at
    }

    /** A new node on [way], between its nodes at [index] (at [from]) and [index] + 1. */
    data class Inserted(
        override val at: GeoPoint,
        val way: OsmData.Way,
        val index: Int,
        val from: GeoPoint,
    ) : Join
}

/** A guided edit, ready for the rider to review (ADR-0012 §4). */
data class EditPlan(
    /** The new road's points between its two joins. */
    val middle: List<GeoPoint>,
    val start: Join,
    val end: Join,
    val lengthM: Double,
) {
    val line: List<GeoPoint> get() = listOf(start.at) + middle + listOf(end.at)
}

/** Why a gap can't become a guided edit; a note is offered instead. */
enum class NoEdit { START_NOT_NEAR_A_ROAD, END_NOT_NEAR_A_ROAD, TOO_LONG, TOO_MANY_NODES, SAME_ROAD_POINT }

/**
 * Plans a new road from the rider's trace (ADR-0012 §4): the gap simplified (Douglas-Peucker,
 * [SIMPLIFY_M]), joined at both ends to roads OSM has now. An end joins an existing node within
 * [SNAP_NODE_M] of the nearest point on the road, else a new node there. Each end's road must be
 * within [JOIN_M] of the gap's end: the rider left it about 25 m before (GapFinder.NEAR_M).
 */
object GuidedEdit {
    const val SIMPLIFY_M = 5.0
    const val JOIN_M = 40.0
    const val SNAP_NODE_M = 3.0
    const val MAX_LENGTH_M = 5_000.0
    const val MAX_NODES = 2_000
    private const val M_PER_DEG = 111_320.0
    private const val MIN_TO_SIMPLIFY = 3

    sealed interface Result {
        data class Ok(
            val plan: EditPlan,
        ) : Result

        data class Refused(
            val why: NoEdit,
        ) : Result
    }

    fun plan(
        gap: Gap,
        data: OsmData,
    ): Result {
        val start = join(gap.points.first(), data)
        val end = join(gap.points.last(), data)
        return if (start == null || end == null) {
            Result.Refused(if (start == null) NoEdit.START_NOT_NEAR_A_ROAD else NoEdit.END_NOT_NEAR_A_ROAD)
        } else {
            checked(gap, start, end)
        }
    }

    private fun checked(
        gap: Gap,
        start: Join,
        end: Join,
    ): Result {
        val middle = simplify(gap.points, SIMPLIFY_M)
        val line = listOf(start.at) + middle + listOf(end.at)
        val length = line.zipWithNext { a, b -> RoadIndex.segmentDistanceM(a, b, b) }.sum()
        val refused =
            when {
                line.size > MAX_NODES -> NoEdit.TOO_MANY_NODES
                length > MAX_LENGTH_M -> NoEdit.TOO_LONG
                start.at == end.at -> NoEdit.SAME_ROAD_POINT
                else -> null
            }
        return refused?.let { Result.Refused(it) } ?: Result.Ok(EditPlan(middle, start, end, length))
    }

    private fun join(
        p: GeoPoint,
        data: OsmData,
    ): Join? {
        var best: Join? = null
        var bestM = JOIN_M
        for (way in data.ways.filter { it.joinable }) {
            way.nodes.zipWithNext().forEachIndexed { i, (ia, ib) ->
                val a = data.nodes[ia] ?: return@forEachIndexed
                val b = data.nodes[ib] ?: return@forEachIndexed
                val q = nearest(p, a.at, b.at)
                val d = RoadIndex.segmentDistanceM(p, q, q)
                if (d <= bestM) {
                    bestM = d
                    best = snap(q, a, b) ?: Join.Inserted(q, way, i, a.at)
                }
            }
        }
        return best
    }

    private fun snap(
        q: GeoPoint,
        a: OsmData.Node,
        b: OsmData.Node,
    ): Join? =
        listOf(a, b)
            .filter { RoadIndex.segmentDistanceM(q, it.at, it.at) <= SNAP_NODE_M }
            .minByOrNull { RoadIndex.segmentDistanceM(q, it.at, it.at) }
            ?.let { Join.Existing(it) }

    /** The point of segment [a]-[b] nearest [p] (flat projection around [p]). */
    private fun nearest(
        p: GeoPoint,
        a: GeoPoint,
        b: GeoPoint,
    ): GeoPoint {
        val k = cos(Math.toRadians(p.lat)) * M_PER_DEG
        val ax = (a.lng - p.lng) * k
        val ay = (a.lat - p.lat) * M_PER_DEG
        val dx = (b.lng - a.lng) * k
        val dy = (b.lat - a.lat) * M_PER_DEG
        val len2 = dx * dx + dy * dy
        val t = if (len2 == 0.0) 0.0 else (-(ax * dx + ay * dy) / len2).coerceIn(0.0, 1.0)
        return GeoPoint(a.lat + (b.lat - a.lat) * t, a.lng + (b.lng - a.lng) * t)
    }

    /** Douglas-Peucker in metres; keeps both ends. */
    fun simplify(
        points: List<GeoPoint>,
        toleranceM: Double,
    ): List<GeoPoint> {
        if (points.size < MIN_TO_SIMPLIFY) return points
        val keep =
            BooleanArray(points.size).also {
                it[0] = true
                it[points.size - 1] = true
            }
        val stack = ArrayDeque(listOf(0 to points.size - 1))
        while (stack.isNotEmpty()) {
            val (from, to) = stack.removeLast()
            var far = -1
            var farM = toleranceM
            for (i in from + 1 until to) {
                val d = RoadIndex.segmentDistanceM(points[i], points[from], points[to])
                if (d > farM) {
                    farM = d
                    far = i
                }
            }
            if (far >= 0) {
                keep[far] = true
                stack.addLast(from to far)
                stack.addLast(far to to)
            }
        }
        return points.filterIndexed { i, _ -> keep[i] }
    }
}
