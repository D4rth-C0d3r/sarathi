package io.github.d4rthc0d3r.sarathi.core.protocol

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.asin
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.sin
import kotlin.math.sqrt
import kotlin.random.Random

class RideFrameTest {
    // Great-circle distance on the mean sphere: good to about 0.5 %, enough to bound ours at
    // 0.1 % only as a cross-check of scale; the exact checks below use known offsets.
    private fun haversine(
        lat1: Double,
        lon1: Double,
        lat2: Double,
        lon2: Double,
    ): Double {
        val r = 6_371_008.8
        val p1 = Math.toRadians(lat1)
        val p2 = Math.toRadians(lat2)
        val dp = p2 - p1
        val dl = Math.toRadians(lon2 - lon1)
        val a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2)
        return 2 * r * asin(sqrt(a))
    }

    @Test
    fun originIsRoundedToAboutAKilometre() {
        val frame = RideFrame.near(15.49871, 73.82785) // Panaji
        val start = frame.project(15.49871, 73.82785)
        assertTrue("start ${hypot(start.x, start.y)} m from the origin", hypot(start.x, start.y) < 800)
        val origin = frame.project(15.50, 73.83)
        assertEquals(0.0, origin.x, 1e-6)
        assertEquals(0.0, origin.y, 1e-6)
    }

    @Test
    fun oneDegreeOfLatitudeAtGoa() {
        // WGS84 meridian arc: about 110.65 km per degree near 15.5 degrees north.
        val frame = RideFrame.near(15.0, 73.83)
        val north = frame.project(16.0, 73.83)
        assertEquals(0.0, north.x, 1e-6)
        assertEquals(110_650.0, north.y, 150.0)
    }

    @Test
    fun distancesAgreeWithTheGlobeWithin150Km() {
        val frame = RideFrame.near(15.5, 73.83)
        for ((dLat, dLon) in listOf(0.5 to 0.0, 0.0 to 0.9, 0.7 to 0.7, -1.0 to 0.5, 0.01 to 0.01)) {
            val p = frame.project(15.5 + dLat, 73.83 + dLon)
            val want = haversine(15.5, 73.83, 15.5 + dLat, 73.83 + dLon)
            assertEquals("($dLat, $dLon)", 1.0, hypot(p.x, p.y) / want, 0.006)
        }
    }

    @Test
    fun aNewFrameBeyond150Km() {
        val frame = RideFrame.near(15.5, 73.83)
        assertFalse(frame.needsNewFrame(16.5, 73.83)) // about 111 km
        assertTrue(frame.needsNewFrame(17.0, 73.83)) // about 166 km
    }

    @Test
    fun epochsAreRandomAndInRange() {
        val epochs = List(200) { RideFrame.near(15.5, 73.83, Random(it)).epoch }
        assertTrue(epochs.all { it in 1..0xFFFF })
        assertTrue(epochs.toSet().size > 190)
    }

    @Test
    fun theDatelineWraps() {
        val frame = RideFrame.near(0.0, 179.99)
        assertTrue(frame.project(0.0, -179.99).x in 1_000.0..3_000.0)
    }

    @Test
    fun lockKeepsStopBelowMove() {
        val ok = Lock(true, 5, 2, 3, 5)
        assertEquals(ok, JsonCodec.decode(MsgType.LOCK, JsonCodec.encode(ok), Sender.PHONE))
        assertTrue(runCatching { Lock(true, 5, 2, 5, 5) }.isFailure)
    }
}
