package io.github.d4rthc0d3r.sarathi.maps.google

import io.github.d4rthc0d3r.sarathi.core.maps.StepManeuver
import com.google.android.libraries.mapsplatform.turnbyturn.model.Maneuver as G

/** Google's turn-by-turn maneuver codes as the protocol's maneuvers. */
internal object GoogleManeuvers {
    @Suppress("CyclomaticComplexMethod") // one branch per group of Google's codes
    fun of(code: Int): StepManeuver =
        when (code) {
            G.DEPART -> StepManeuver.DEPART
            G.DESTINATION, G.DESTINATION_LEFT, G.DESTINATION_RIGHT -> StepManeuver.ARRIVE
            G.TURN_LEFT -> StepManeuver.LEFT
            G.TURN_RIGHT -> StepManeuver.RIGHT
            G.TURN_KEEP_LEFT -> StepManeuver.KEEP_LEFT
            G.TURN_KEEP_RIGHT -> StepManeuver.KEEP_RIGHT
            G.TURN_SLIGHT_LEFT -> StepManeuver.SLIGHT_LEFT
            G.TURN_SLIGHT_RIGHT -> StepManeuver.SLIGHT_RIGHT
            G.TURN_SHARP_LEFT -> StepManeuver.SHARP_LEFT
            G.TURN_SHARP_RIGHT -> StepManeuver.SHARP_RIGHT
            G.TURN_U_TURN_CLOCKWISE, G.TURN_U_TURN_COUNTERCLOCKWISE -> StepManeuver.UTURN
            G.MERGE_UNSPECIFIED, G.MERGE_LEFT, G.MERGE_RIGHT -> StepManeuver.MERGE
            G.FORK_LEFT -> StepManeuver.FORK_LEFT
            G.FORK_RIGHT -> StepManeuver.FORK_RIGHT
            in G.ON_RAMP_UNSPECIFIED..G.OFF_RAMP_U_TURN_COUNTERCLOCKWISE -> ramp(code)
            in G.ROUNDABOUT_CLOCKWISE..G.ROUNDABOUT_EXIT_COUNTERCLOCKWISE -> StepManeuver.ROUNDABOUT
            G.FERRY_BOAT, G.FERRY_TRAIN -> StepManeuver.FERRY
            else -> StepManeuver.STRAIGHT // straight, name change, unknown
        }

    /** Ramps by side; unspecified ones keep left, as India drives on the left. */
    private fun ramp(code: Int): StepManeuver =
        when (code) {
            G.ON_RAMP_RIGHT, G.ON_RAMP_KEEP_RIGHT, G.ON_RAMP_SLIGHT_RIGHT, G.ON_RAMP_SHARP_RIGHT,
            G.OFF_RAMP_RIGHT, G.OFF_RAMP_KEEP_RIGHT, G.OFF_RAMP_SLIGHT_RIGHT, G.OFF_RAMP_SHARP_RIGHT,
            -> StepManeuver.RAMP_RIGHT

            else -> StepManeuver.RAMP_LEFT
        }
}
