package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import io.github.d4rthc0d3r.sarathi.core.mapdata.MapLevel
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.TestMaps.at
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.TestMaps.tiles
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class GapFileTest {
    private val gap = Gap(listOf(at(100.0, 700.0), at(100.0, 710.0), at(100.0, 720.0)), 20.0, 10, 20)

    @Test
    fun `gaps survive being written and read`() {
        val stored = StoredGap("track-1.csv", gap, GapStatus.OPEN)
        val back = GapFile.read("track-1.csv", sequenceOf(GapFile.line(stored), "OPEN;broken", ""))
        assertEquals(1, back.size)
        assertEquals(GapStatus.OPEN, back[0].status)
        assertEquals(gap.startUtcMs, back[0].gap.startUtcMs)
        assertEquals(gap.points[2].lng, back[0].gap.points[2].lng, 1e-6)
        assertEquals("track-1.csv@10", back[0].id)
    }

    @Test
    fun `tiles around a track include the neighbours`() {
        assertEquals(9, OsmTiles.around(listOf(at(0.0, 0.0))).size)
        assertTrue(OsmTiles.around(listOf(at(0.0, 0.0))).all { it.level == MapLevel.L0 })
    }

    @Test
    fun `only detailed OSM tiles give roads`() {
        val road = listOf(at(0.0, 0.0), at(0.0, 500.0))
        assertEquals(1, OsmTiles.roads(tiles(listOf(road))).size)
        assertTrue(OsmTiles.roads(tiles(listOf(road), level = MapLevel.L1)).isEmpty())
    }
}
