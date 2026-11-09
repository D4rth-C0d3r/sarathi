package io.github.d4rthc0d3r.sarathi.core.maps

import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.add
import kotlinx.serialization.json.buildJsonArray
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.put
import kotlinx.serialization.json.putJsonArray
import kotlinx.serialization.json.putJsonObject

/**
 * Our route as one OSRM route object, for Ferrostar's guidance (ADR-0006): one leg, one OSRM step
 * per [RouteStep], each with its stretch of the line (polyline6). Durations follow distance. A
 * middle stop's arrival is written as `notification`, so only the last step is the arrival.
 */
object OsrmJson {
    private const val PRECISION = Osrm.POLYLINE6

    fun route(route: Route): JsonObject {
        val steps = route.steps
        val perMetre = if (route.distanceM > 0) route.durationS / route.distanceM else 0.0
        return buildJsonObject {
            put("distance", route.distanceM)
            put("duration", route.durationS.toDouble())
            put("weight", route.durationS.toDouble())
            put("weight_name", "duration")
            put("geometry", Polyline.encode(route.points, PRECISION))
            putJsonArray("legs") {
                add(
                    buildJsonObject {
                        put("distance", route.distanceM)
                        put("duration", route.durationS.toDouble())
                        put("summary", route.via)
                        putJsonArray("steps") { steps.forEachIndexed { i, s -> add(step(route, i, s, perMetre)) } }
                    },
                )
            }
        }
    }

    private fun step(
        route: Route,
        index: Int,
        s: RouteStep,
        perMetre: Double,
    ): JsonObject {
        val end = route.steps.getOrNull(index + 1)?.pointIndex ?: route.points.lastIndex
        val line =
            route.points.subList(s.pointIndex, maxOf(end, s.pointIndex) + 1).let {
                if (it.size <
                    2
                ) {
                    it + it
                } else {
                    it
                }
            }
        val at = route.points[s.pointIndex]
        val last = index == route.steps.lastIndex
        val (type, modifier) = osrm(s.maneuver, last)
        return buildJsonObject {
            put("distance", s.distanceM)
            put("duration", s.distanceM * perMetre)
            put("weight", s.distanceM * perMetre)
            put("geometry", Polyline.encode(line, PRECISION))
            put("name", s.roadName)
            put("mode", "driving")
            put("driving_side", "left") // India
            putJsonObject("maneuver") {
                put("type", type)
                modifier?.let { put("modifier", it) }
                put(
                    "location",
                    buildJsonArray {
                        add(at.lng)
                        add(at.lat)
                    },
                )
                put("bearing_before", 0)
                put("bearing_after", 0)
                if (s.roundaboutExit > 0) put("exit", s.roundaboutExit)
            }
            putJsonArray("intersections") {
                add(
                    buildJsonObject {
                        put(
                            "location",
                            buildJsonArray {
                                add(at.lng)
                                add(at.lat)
                            },
                        )
                        put("bearings", buildJsonArray { add(0) })
                        put("entry", buildJsonArray { add(true) })
                        put("out", 0)
                    },
                )
            }
        }
    }

    /** The inverse of [Osrm.maneuver], as near as OSRM's vocabulary allows. */
    @Suppress("CyclomaticComplexMethod") // one line per maneuver
    fun osrm(
        m: StepManeuver,
        lastStep: Boolean,
    ): Pair<String, String?> =
        when (m) {
            StepManeuver.DEPART -> "depart" to null
            StepManeuver.ARRIVE -> if (lastStep) "arrive" to null else "notification" to "straight"
            StepManeuver.STRAIGHT -> "continue" to "straight"
            StepManeuver.SLIGHT_LEFT -> "turn" to "slight left"
            StepManeuver.LEFT -> "turn" to "left"
            StepManeuver.SHARP_LEFT -> "turn" to "sharp left"
            StepManeuver.SLIGHT_RIGHT -> "turn" to "slight right"
            StepManeuver.RIGHT -> "turn" to "right"
            StepManeuver.SHARP_RIGHT -> "turn" to "sharp right"
            StepManeuver.UTURN -> "turn" to "uturn"
            StepManeuver.ROUNDABOUT -> "roundabout" to null
            StepManeuver.KEEP_LEFT -> "continue" to "slight left"
            StepManeuver.KEEP_RIGHT -> "continue" to "slight right"
            StepManeuver.MERGE -> "merge" to "straight"
            StepManeuver.FORK_LEFT -> "fork" to "slight left"
            StepManeuver.FORK_RIGHT -> "fork" to "slight right"
            StepManeuver.RAMP_LEFT -> "off ramp" to "left"
            StepManeuver.RAMP_RIGHT -> "off ramp" to "right"
            StepManeuver.FERRY -> "notification" to "straight"
        }
}
