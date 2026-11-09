package io.github.d4rthc0d3r.sarathi.maps.google

import io.github.d4rthc0d3r.sarathi.core.maps.StepManeuver
import org.junit.Assert.assertEquals
import org.junit.Test
import com.google.android.libraries.mapsplatform.turnbyturn.model.Maneuver as G

class GoogleManeuversTest {
    @Test
    fun everyGoogleCodeHasAManeuver() {
        assertEquals(StepManeuver.DEPART, GoogleManeuvers.of(G.DEPART))
        assertEquals(StepManeuver.ARRIVE, GoogleManeuvers.of(G.DESTINATION_RIGHT))
        assertEquals(StepManeuver.UTURN, GoogleManeuvers.of(G.TURN_U_TURN_COUNTERCLOCKWISE))
        assertEquals(StepManeuver.ROUNDABOUT, GoogleManeuvers.of(G.ROUNDABOUT_LEFT_CLOCKWISE))
        assertEquals(StepManeuver.ROUNDABOUT, GoogleManeuvers.of(G.ROUNDABOUT_EXIT_COUNTERCLOCKWISE))
        assertEquals(StepManeuver.RAMP_RIGHT, GoogleManeuvers.of(G.OFF_RAMP_SLIGHT_RIGHT))
        assertEquals(StepManeuver.RAMP_LEFT, GoogleManeuvers.of(G.ON_RAMP_UNSPECIFIED))
        assertEquals(StepManeuver.FERRY, GoogleManeuvers.of(G.FERRY_TRAIN))
        assertEquals(StepManeuver.STRAIGHT, GoogleManeuvers.of(G.NAME_CHANGE))
        assertEquals(StepManeuver.STRAIGHT, GoogleManeuvers.of(999)) // a code from a newer SDK
    }
}
