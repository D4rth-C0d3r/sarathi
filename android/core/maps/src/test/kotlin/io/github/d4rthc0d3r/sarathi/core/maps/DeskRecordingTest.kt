package io.github.d4rthc0d3r.sarathi.core.maps

import io.github.d4rthc0d3r.sarathi.core.maps.TrafficLevel.FREE
import io.github.d4rthc0d3r.sarathi.core.maps.TrafficLevel.JAM
import io.github.d4rthc0d3r.sarathi.core.maps.TrafficLevel.SLOW
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.test.runTest
import kotlinx.serialization.json.Json
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class DeskRecordingTest {
    private val route =
        Route(
            RoutingBackend.MAPPLS,
            listOf(LatLng(15.49, 73.83), LatLng(15.55, 73.75)),
            listOf(RouteStep(StepManeuver.DEPART, 0, "", 1_000.0), RouteStep(StepManeuver.ARRIVE, 1, "", 0.0)),
            1_000.0,
            600,
            TravelMode.TWO_WHEELER,
            "Powered by Mappls",
            traffic = TrafficUpdate(listOf(TrafficStretch(0.0, 1_000.0, FREE)), 660),
        )

    @Test
    fun overlayCutsAStretchIntoTheBase() {
        val s = TrafficScript.overlay(listOf(TrafficStretch(0.0, 1_000.0, FREE)), 350.0, 400.0, JAM)
        assertEquals(
            listOf(
                TrafficStretch(0.0, 350.0, FREE),
                TrafficStretch(350.0, 50.0, JAM),
                TrafficStretch(400.0, 600.0, FREE),
            ),
            s,
        )
        assertEquals(listOf(TrafficStretch(0.0, 1_000.0, FREE)), TrafficScript.overlay(s, 350.0, 400.0, FREE))
    }

    @Test
    fun theScriptJamsSlowsAndClears() =
        runTest {
            val updates = TrafficScript.updates(route, stepMs = 1).toList()
            assertEquals(3, updates.size)
            assertTrue(updates[0].stretches.any { it.level == JAM })
            assertTrue(updates[1].stretches.any { it.level == SLOW })
            assertFalse(updates[2].stretches.any { it.level == JAM })
            updates.forEach { u -> assertEquals(1_000.0, u.stretches.sumOf { it.lengthM }, 0.01) }
        }

    @Test
    fun aRecordingSurvivesJsonAndGoogleIsNotRecorded() {
        val r = DeskRecording("Calangute", route, RouteRequest(LatLng(15.49, 73.83), listOf(LatLng(15.55, 73.75))), 1L)
        assertEquals(
            r,
            Json.decodeFromString(DeskRecording.serializer(), Json.encodeToString(DeskRecording.serializer(), r)),
        )
        assertTrue(DeskRecording.canRecord(route))
        assertFalse(DeskRecording.canRecord(route.copy(backend = RoutingBackend.GOOGLE)))
    }
}
