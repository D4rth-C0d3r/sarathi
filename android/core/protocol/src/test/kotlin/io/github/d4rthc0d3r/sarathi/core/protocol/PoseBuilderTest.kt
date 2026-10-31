package io.github.d4rthc0d3r.sarathi.core.protocol

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random

class PoseBuilderTest {
    private fun builder() = PoseBuilder { lat, lon -> RideFrame.near(lat, lon, Random(7)) }

    @Test
    fun firstFixStartsAFrame() {
        val b = builder()
        val pose = b.pose(Fix(15.5, 73.83, 1_000, speedMps = 13.9f, bearingDeg = 270.5f, accuracyM = 4.4f))
        assertTrue(b.frameChanged)
        assertEquals(b.frame?.epoch, pose.epoch)
        assertEquals(0, pose.xDm) // the origin is the rounded start, here the start itself
        assertEquals(1390, pose.speedCms)
        assertEquals(27050, pose.headingCdeg)
        assertEquals(4, pose.accM)
        b.pose(Fix(15.5, 73.83, 2_000))
        assertFalse(b.frameChanged)
    }

    @Test
    fun speedFromDistanceWhenTheGpsHasNone() {
        val b = builder()
        b.pose(Fix(15.5, 73.83, 0))
        val pose = b.pose(Fix(15.5 + 0.0001, 73.83, 1_000)) // about 11 m north in 1 s
        assertEquals(1106.0, pose.speedCms.toDouble(), 15.0)
        assertEquals(Pose.ACC_UNKNOWN, pose.accM)
        assertEquals(11.06, pose.yDm / 10.0, 0.2) // metres
    }

    @Test
    fun headingHoldsWithoutABearing() {
        val b = builder()
        b.pose(Fix(15.5, 73.83, 0, bearingDeg = 90f))
        assertEquals(9000, b.pose(Fix(15.5, 73.83, 1_000)).headingCdeg)
        assertEquals(0, b.pose(Fix(15.5, 73.83, 2_000, bearingDeg = 360f)).headingCdeg)
    }

    @Test
    fun aNewFrameBeyond150Km() {
        val b = builder()
        b.pose(Fix(15.5, 73.83, 0))
        b.pose(Fix(17.0, 73.83, 1_000))
        assertTrue(b.frameChanged)
        assertEquals(0, b.pose(Fix(17.0, 73.83, 2_000)).yDm)
    }
}
