package io.github.d4rthc0d3r.sarathi.core.maps

/** One step of a route in OSRM's format (Mappls's directions; Valhalla can answer in it too). */
data class OsrmStep(
    val type: String,
    val modifier: String?,
    val exit: Int?,
    val name: String,
    val distanceM: Double,
    /** The step's own line, encoded at [Osrm.route]'s precision. */
    val geometry: String,
)

/** OSRM-format routes as ours: steps' lines joined into one, maneuvers as the protocol names them. */
object Osrm {
    @Suppress("LongParameterList") // a route's parts, as the backends give them
    fun route(
        backend: RoutingBackend,
        legs: List<List<OsrmStep>>,
        distanceM: Double,
        durationS: Double,
        mode: TravelMode,
        attribution: String,
        precision: Int = POLYLINE6,
    ): Route {
        val points = mutableListOf<LatLng>()
        val steps = mutableListOf<RouteStep>()
        legs.forEachIndexed { legIndex, leg ->
            leg.forEach { s ->
                val line = Polyline.decode(s.geometry, precision)
                // Consecutive steps share their joining point; repeated points are kept once.
                val start =
                    if (points.isNotEmpty() &&
                        line.firstOrNull() == points.last()
                    ) {
                        points.lastIndex
                    } else {
                        points.size
                    }
                line.forEach { if (points.lastOrNull() != it) points += it }
                val maneuver = maneuver(s.type, s.modifier, firstLeg = legIndex == 0)
                if (maneuver == null) {
                    steps.lastOrNull()?.let { steps[steps.lastIndex] = it.copy(distanceM = it.distanceM + s.distanceM) }
                } else {
                    steps +=
                        RouteStep(
                            maneuver,
                            start.coerceAtMost(points.lastIndex.coerceAtLeast(0)),
                            s.name,
                            s.distanceM,
                            s.exit ?: 0,
                        )
                }
            }
        }
        return Route(
            backend,
            points,
            steps,
            distanceM,
            durationS.toLong(),
            mode,
            attribution,
            via = Routes.mainRoad(steps),
        )
    }

    /**
     * OSRM's maneuver type and modifier. A roundabout's exit step and a later leg's departure
     * belong to the step before them (null).
     */
    @Suppress("CyclomaticComplexMethod") // one line per OSRM type
    fun maneuver(
        type: String,
        modifier: String?,
        firstLeg: Boolean,
    ): StepManeuver? =
        when (type) {
            "depart" -> {
                if (firstLeg) StepManeuver.DEPART else null
            }

            "arrive" -> {
                StepManeuver.ARRIVE
            }

            "roundabout", "rotary", "roundabout turn" -> {
                StepManeuver.ROUNDABOUT
            }

            "exit roundabout", "exit rotary" -> {
                null
            }

            "merge" -> {
                StepManeuver.MERGE
            }

            "fork" -> {
                sided(modifier, StepManeuver.FORK_LEFT, StepManeuver.FORK_RIGHT)
            }

            "on ramp", "off ramp" -> {
                sided(modifier, StepManeuver.RAMP_LEFT, StepManeuver.RAMP_RIGHT)
            }

            "continue" -> {
                when (modifier) {
                    "slight left" -> StepManeuver.KEEP_LEFT
                    "slight right" -> StepManeuver.KEEP_RIGHT
                    else -> turn(modifier)
                }
            }

            else -> {
                turn(modifier)
            } // turn, end of road, new name, notification
        }

    private fun sided(
        modifier: String?,
        left: StepManeuver,
        right: StepManeuver,
    ): StepManeuver =
        when {
            modifier?.contains("left") == true -> left
            modifier?.contains("right") == true -> right
            else -> StepManeuver.STRAIGHT
        }

    private fun turn(modifier: String?): StepManeuver =
        when (modifier) {
            "uturn" -> StepManeuver.UTURN
            "sharp right" -> StepManeuver.SHARP_RIGHT
            "right" -> StepManeuver.RIGHT
            "slight right" -> StepManeuver.SLIGHT_RIGHT
            "slight left" -> StepManeuver.SLIGHT_LEFT
            "left" -> StepManeuver.LEFT
            "sharp left" -> StepManeuver.SHARP_LEFT
            else -> StepManeuver.STRAIGHT
        }

    const val POLYLINE6 = 6
}

object Routes {
    /** The longest named stretch, for "via NH 66" on the confirmation screen. */
    fun mainRoad(steps: List<RouteStep>): String =
        steps
            .filter { it.roadName.isNotBlank() }
            .groupBy { it.roadName }
            .maxByOrNull { (_, s) -> s.sumOf { it.distanceM } }
            ?.key
            .orEmpty()
}
