package io.github.d4rthc0d3r.sarathi.core.mapdata

import kotlin.math.max
import kotlin.math.min

/**
 * Geometry in tile units (0..4096, y down), exactly as tools/make_chunks.py does it: the same
 * order of work and the same arithmetic, so the phone's chunks match the reference byte for byte.
 */
internal object TileGeometry {
    const val EXTENT = 4096.0

    /** A point with -0.0 turned into 0.0, so equal points are equal keys (as in Python). */
    fun pt(
        x: Double,
        y: Double,
    ) = TilePoint(x + 0.0, y + 0.0)

    /** Parts of a polyline inside the tile (Liang-Barsky per segment). */
    fun clipLine(points: List<TilePoint>): List<List<TilePoint>> {
        val parts = mutableListOf<List<TilePoint>>()
        var current = mutableListOf<TilePoint>()
        for (k in 0 until points.size - 1) {
            val (x0, y0) = points[k]
            val (x1, y1) = points[k + 1]
            val dx = x1 - x0
            val dy = y1 - y0
            val range = clipRange(dx, dy, x0, y0)
            if (range == null) {
                if (current.size >= 2) parts += current
                current = mutableListOf()
                continue
            }
            val (t0, t1) = range
            val a = pt(x0 + t0 * dx, y0 + t0 * dy)
            val b = pt(x0 + t1 * dx, y0 + t1 * dy)
            if (current.isEmpty() || current.last() != a) {
                if (current.size >= 2) parts += current
                current = mutableListOf(a)
            }
            current += b
            if (t1 < 1.0) {
                if (current.size >= 2) parts += current
                current = mutableListOf()
            }
        }
        if (current.size >= 2) parts += current
        return parts
    }

    /** The part of a segment inside the tile, as (t0, t1) along it, or null if none. */
    private fun clipRange(
        dx: Double,
        dy: Double,
        x0: Double,
        y0: Double,
    ): Pair<Double, Double>? {
        var t0 = 0.0
        var t1 = 1.0
        val edges = arrayOf(-dx to x0, dx to EXTENT - x0, -dy to y0, dy to EXTENT - y0)
        var outside = false
        for ((p, q) in edges) {
            if (outside) break
            if (p == 0.0) {
                outside = q < 0
            } else {
                val r = q / p
                if (p < 0) t0 = max(t0, r) else t1 = min(t1, r)
                outside = t0 > t1
            }
        }
        return if (outside) null else t0 to t1
    }

    /** Sutherland-Hodgman against the tile's four edges. */
    fun clipPolygon(ring: List<TilePoint>): List<TilePoint> {
        fun atX(xe: Double) = { a: TilePoint, b: TilePoint -> pt(xe, a.y + (b.y - a.y) * (xe - a.x) / (b.x - a.x)) }

        fun atY(ye: Double) = { a: TilePoint, b: TilePoint -> pt(a.x + (b.x - a.x) * (ye - a.y) / (b.y - a.y), ye) }
        val passes =
            listOf<Pair<(TilePoint) -> Boolean, (TilePoint, TilePoint) -> TilePoint>>(
                { p: TilePoint -> p.x >= 0 } to atX(0.0),
                { p: TilePoint -> p.x <= EXTENT } to atX(EXTENT),
                { p: TilePoint -> p.y >= 0 } to atY(0.0),
                { p: TilePoint -> p.y <= EXTENT } to atY(EXTENT),
            )
        var pts = ring
        for ((inside, cross) in passes) {
            if (pts.isEmpty()) break
            pts = clipAgainst(pts, inside, cross)
        }
        return pts
    }

    private fun clipAgainst(
        pts: List<TilePoint>,
        inside: (TilePoint) -> Boolean,
        cross: (TilePoint, TilePoint) -> TilePoint,
    ): List<TilePoint> {
        val out = mutableListOf<TilePoint>()
        for (i in pts.indices) {
            val cur = pts[i]
            val prev = pts[if (i == 0) pts.size - 1 else i - 1]
            if (inside(cur)) {
                if (!inside(prev)) out += cross(prev, cur)
                out += cur
            } else if (inside(prev)) {
                out += cross(prev, cur)
            }
        }
        return out
    }

    /** Twice the signed area (positive for a vector tile's outer ring, y down). */
    fun area2(ring: List<TilePoint>): Double {
        var sum = 0.0
        for (i in ring.indices) {
            val a = ring[i]
            val b = ring[(i + 1) % ring.size]
            sum += a.x * b.y - b.x * a.y
        }
        return sum
    }

    /** Joins lines of one class where exactly two line ends meet, in input order. */
    @Suppress("CyclomaticComplexMethod", "NestedBlockDepth", "LoopWithTooManyJumpStatements")
    fun mergeLines(input: List<List<TilePoint>>): List<List<TilePoint>> {
        val lines = input.map { it.toList() }.toMutableList()
        val ends = HashMap<TilePoint, MutableList<Int>>()
        lines.forEachIndexed { i, l ->
            ends.getOrPut(l.first()) { mutableListOf() } += i
            ends.getOrPut(l.last()) { mutableListOf() } += i
        }
        val alive = BooleanArray(lines.size) { true }
        for (i in lines.indices) {
            if (!alive[i]) continue
            for (atEnd in listOf(true, false)) {
                while (true) {
                    val line = lines[i]
                    val p = if (atEnd) line.last() else line.first()
                    val at = ends[p].orEmpty()
                    val others = at.filter { it != i && alive[it] }
                    if (at.size != 2 || others.size != 1 || line.first() == line.last()) break
                    val j = others[0]
                    val other = lines[j]
                    if (other.first() == other.last()) break
                    val seg: List<TilePoint>
                    if (atEnd) {
                        seg = if (other.first() == p) other else other.reversed()
                        lines[i] = line + seg.drop(1)
                    } else {
                        seg = if (other.last() == p) other else other.reversed()
                        lines[i] = seg.dropLast(1) + line
                    }
                    alive[j] = false
                    val far = if (atEnd) seg.last() else seg.first()
                    ends[far] = ends.getValue(far).map { if (it == j) i else it }.toMutableList()
                    ends[p] = mutableListOf()
                }
            }
        }
        return lines.filterIndexed { i, _ -> alive[i] }
    }

    /** Douglas-Peucker, iterative, keeping both ends. */
    fun simplify(
        points: List<TilePoint>,
        tol: Double,
    ): List<TilePoint> {
        if (points.size < MIN_SIMPLIFY) return points
        val keep = BooleanArray(points.size)
        keep[0] = true
        keep[points.size - 1] = true
        val stack = ArrayDeque<Pair<Int, Int>>()
        stack.addLast(0 to points.size - 1)
        val tol2 = tol * tol
        while (stack.isNotEmpty()) {
            val (a, b) = stack.removeLast()
            val (ax, ay) = points[a]
            val (bx, by) = points[b]
            val dx = bx - ax
            val dy = by - ay
            val len2 = dx * dx + dy * dy
            var best = -1.0
            var index = -1
            for (k in a + 1 until b) {
                val (px, py) = points[k]
                val d2 =
                    if (len2 == 0.0) {
                        sq(px - ax) + sq(py - ay)
                    } else {
                        val t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / len2))
                        sq(px - ax - t * dx) + sq(py - ay - t * dy)
                    }
                if (d2 > best) {
                    best = d2
                    index = k
                }
            }
            if (best > tol2) {
                keep[index] = true
                stack.addLast(a to index)
                stack.addLast(index to b)
            }
        }
        return points.filterIndexed { i, _ -> keep[i] }
    }

    private fun sq(v: Double) = v * v

    private const val MIN_SIMPLIFY = 3 // two points are already as simple as they get
}
