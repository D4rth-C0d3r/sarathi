package io.github.d4rthc0d3r.sarathi.link

import io.github.d4rthc0d3r.sarathi.core.maps.GuidanceState
import io.github.d4rthc0d3r.sarathi.core.maps.MeterStatus
import io.github.d4rthc0d3r.sarathi.core.maps.Skipped
import io.github.d4rthc0d3r.sarathi.core.maps.TravelMode
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable
import io.github.d4rthc0d3r.sarathi.core.protocol.BackendInfo
import io.github.d4rthc0d3r.sarathi.core.protocol.DashText
import io.github.d4rthc0d3r.sarathi.core.protocol.Event
import io.github.d4rthc0d3r.sarathi.core.protocol.EventKind
import io.github.d4rthc0d3r.sarathi.core.protocol.FallbackReason
import io.github.d4rthc0d3r.sarathi.core.protocol.LocalPoint
import io.github.d4rthc0d3r.sarathi.core.protocol.Maneuver
import io.github.d4rthc0d3r.sarathi.core.protocol.Nav
import io.github.d4rthc0d3r.sarathi.core.protocol.NavFlag
import io.github.d4rthc0d3r.sarathi.core.protocol.Point
import io.github.d4rthc0d3r.sarathi.core.protocol.RideFrame
import io.github.d4rthc0d3r.sarathi.core.protocol.RouteMarker
import io.github.d4rthc0d3r.sarathi.core.protocol.Routing
import io.github.d4rthc0d3r.sarathi.core.protocol.Search
import io.github.d4rthc0d3r.sarathi.core.maps.Route as MapsRoute
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend as MapsRouting
import io.github.d4rthc0d3r.sarathi.core.maps.SearchBackend as MapsSearch
import io.github.d4rthc0d3r.sarathi.core.protocol.Route as DashRoute

/**
 * A ride as protocol v1 messages (protocol section 4, ADR-0007, ADR-0009): the whole route once,
 * in the ride's frame; guidance as `nav`; the backend; fallback, reroute and cost events.
 */
object RideMessages {
    /** Route points in whole metres. */
    const val QUANTUM_DM = 10
    private const val DM_PER_M = 10.0
    private const val FIRST_TOLERANCE_M = 1.0

    /**
     * [route] in the dash's units: projected with [frame], thinned to the protocol's 4,000 points
     * (the points where maneuvers happen always stay), its steps as markers, nearest first.
     */
    fun route(
        route: MapsRoute,
        frame: RideFrame,
        routeId: Long,
        version: Int,
    ): DashRoute {
        val local = route.points.map { frame.project(it.lat, it.lng) }
        val keep = route.steps.map { it.pointIndex.coerceIn(0, local.lastIndex) }.toSortedSet()
        var tolerance = FIRST_TOLERANCE_M
        var kept = Simplify.indices(local, keep, tolerance)
        while (kept.size > DashRoute.MAX_POINTS) {
            tolerance *= 2
            kept = Simplify.indices(local, keep, tolerance)
        }
        // Whole-metre points; a repeat of the point before is dropped and its index points back.
        val points = mutableListOf<Point>()
        val newIndex = HashMap<Int, Int>()
        for (i in kept) {
            val p =
                local[i].let {
                    Point(
                        Math.round(it.x * DM_PER_M / QUANTUM_DM).toInt(),
                        Math.round(it.y * DM_PER_M / QUANTUM_DM).toInt(),
                    )
                }
            if (points.lastOrNull() != p) points += p
            newIndex[i] = points.lastIndex
        }
        if (points.size < 2) points += points.last()
        val markers =
            route.steps
                .map {
                    RouteMarker(
                        newIndex.getValue(it.pointIndex.coerceIn(0, local.lastIndex)),
                        maneuver(it.maneuver).code,
                        it.roundaboutExit,
                    )
                }.distinctBy { it.point }
                .take(DashRoute.MAX_MARKERS)
        return DashRoute(
            routeId = routeId,
            version = version,
            backend = routing(route.backend),
            epoch = frame.epoch,
            mode = if (route.mode == TravelMode.TWO_WHEELER) DashRoute.MODE_TWO_WHEELER else DashRoute.MODE_DRIVING,
            quantumDm = QUANTUM_DM,
            points = points,
            markers = markers,
        )
    }

    fun nav(g: GuidanceState): Nav =
        Nav(
            m = maneuver(g.maneuver).wire,
            exit = g.exit.coerceIn(0, MAX_EXIT),
            distM = Math.round(g.distanceToManeuverM).coerceIn(0, MAX_DIST_M),
            street = DashText.of(g.roadName, Nav.STREET_BYTES),
            remS = g.remainingS.coerceIn(0, MAX_REM_S),
            remM = Math.round(g.remainingM).coerceIn(0, MAX_REM_M),
            flags =
                buildSet {
                    if (g.offRoute) add(NavFlag.OFF_ROUTE)
                    if (g.rerouting) add(NavFlag.REROUTING)
                    if (g.arrived) add(NavFlag.ARRIVED)
                },
        )

    fun backend(
        route: MapsRoute,
        search: MapsSearch,
        name: String,
    ): BackendInfo =
        BackendInfo(
            routing = routing(route.backend),
            search = search(search),
            name = DashText.of(name, BackendInfo.NAME_BYTES),
            attribution = DashText.of(route.attribution, BackendInfo.ATTRIBUTION_BYTES),
            traffic = route.traffic != null,
            twoWheeler = route.mode == TravelMode.TWO_WHEELER,
        )

    /** One `fallback` event per backend passed over for a reason the rider should know. */
    fun fallbacks(
        skipped: List<Skipped<MapsRouting>>,
        to: MapsRouting,
    ): List<Event> =
        skipped.mapNotNull { s ->
            fallbackReason(
                s.reason,
            )?.let { Event(EventKind.FALLBACK, from = routing(s.backend), to = routing(to), reason = it) }
        }

    /** The cost guard's warning (80 % and over) for a Google or Mappls meter. */
    fun costWarning(
        status: MeterStatus,
        search: MapsSearch,
    ): Event? {
        val percent = if (status.cap > 0) (status.used * PERCENT / status.cap).coerceIn(0, PERCENT) else 0
        return if (percent >=
            WARN_PERCENT
        ) {
            Event(EventKind.COST_WARNING, backend = search(search), percent = percent)
        } else {
            null
        }
    }

    fun reroute(): Event = Event(EventKind.REROUTE)

    fun routing(b: MapsRouting): Routing =
        when (b) {
            MapsRouting.GOOGLE -> Routing.GOOGLE
            MapsRouting.MAPPLS -> Routing.MAPPLS
            MapsRouting.OSM_VALHALLA, MapsRouting.OSM_GRAPHHOPPER -> Routing.OSM_ONLINE
            MapsRouting.OSM_OFFLINE -> Routing.OSM_OFFLINE
        }

    private fun search(b: MapsSearch): Search =
        when (b) {
            MapsSearch.GOOGLE -> Search.GOOGLE
            MapsSearch.MAPPLS -> Search.MAPPLS
            MapsSearch.OSM -> Search.OSM
        }

    /** Ours and the protocol's maneuvers share their names. */
    private fun maneuver(m: io.github.d4rthc0d3r.sarathi.core.maps.StepManeuver): Maneuver = Maneuver.valueOf(m.name)

    /** Missing keys and terms are settings, not fallbacks; everything else is worth a notice. */
    private fun fallbackReason(u: Unavailable): FallbackReason? =
        when (u) {
            Unavailable.NO_KEY, Unavailable.NO_TERMS, Unavailable.NO_DATA -> null
            Unavailable.OVER_QUOTA -> FallbackReason.QUOTA
            Unavailable.OFFLINE, Unavailable.TIMEOUT -> FallbackReason.OFFLINE
            Unavailable.NO_ROUTE -> FallbackReason.NO_ROUTE
            Unavailable.ACCOUNT -> FallbackReason.ACCOUNT
            Unavailable.ERROR -> FallbackReason.ERROR
        }

    private const val MAX_EXIT = 15
    private const val MAX_DIST_M = 1_000_000L
    private const val MAX_REM_S = 604_800L
    private const val MAX_REM_M = 10_000_000L
    private const val PERCENT = 100
    private const val WARN_PERCENT = 80
}

/** Douglas–Peucker on a projected line, never dropping the [keep] points. */
internal object Simplify {
    fun indices(
        points: List<LocalPoint>,
        keep: Set<Int>,
        toleranceM: Double,
    ): List<Int> {
        if (points.size <= 2) return points.indices.toList()
        val anchors = (keep + 0 + points.lastIndex).filter { it in points.indices }.sorted()
        val out = sortedSetOf<Int>()
        anchors.zipWithNext().forEach { (a, b) -> dp(points, a, b, toleranceM, out) }
        out += anchors
        return out.toList()
    }

    private fun dp(
        p: List<LocalPoint>,
        first: Int,
        last: Int,
        tolerance: Double,
        out: MutableSet<Int>,
    ) {
        val stack = ArrayDeque(listOf(first to last))
        while (stack.isNotEmpty()) {
            val (a, b) = stack.removeLast()
            var worst = -1
            var worstD = tolerance
            for (i in a + 1 until b) {
                val d = distance(p[i], p[a], p[b])
                if (d > worstD) {
                    worst = i
                    worstD = d
                }
            }
            if (worst >= 0) {
                out += worst
                stack += a to worst
                stack += worst to b
            }
        }
    }

    /** From [p] to the segment [a]–[b]. */
    private fun distance(
        p: LocalPoint,
        a: LocalPoint,
        b: LocalPoint,
    ): Double {
        val dx = b.x - a.x
        val dy = b.y - a.y
        val len2 = dx * dx + dy * dy
        val t = if (len2 == 0.0) 0.0 else (((p.x - a.x) * dx + (p.y - a.y) * dy) / len2).coerceIn(0.0, 1.0)
        val ex = a.x + t * dx - p.x
        val ey = a.y + t * dy - p.y
        return Math.sqrt(ex * ex + ey * ey)
    }
}
