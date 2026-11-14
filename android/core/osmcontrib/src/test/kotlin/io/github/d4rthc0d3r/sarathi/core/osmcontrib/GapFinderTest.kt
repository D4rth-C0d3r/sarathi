package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import io.github.d4rthc0d3r.sarathi.core.mapdata.MapLevel
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.TestMaps.at
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.TestMaps.ride
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.TestMaps.tiles
import io.github.d4rthc0d3r.sarathi.core.protocol.Provenance
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class GapFinderTest {
    // A road going east for 2 km.
    private val road = listOf(at(0.0, 0.0), at(0.0, 2000.0))

    // Ride along it, leave it 100 m north for 400 m, come back.
    private val detour =
        listOf(at(0.0, 0.0), at(0.0, 600.0), at(100.0, 700.0), at(100.0, 1100.0), at(0.0, 1200.0), at(0.0, 2000.0))

    @Test
    fun `riding on the road finds nothing`() {
        assertTrue(GapFinder.find(ride(road), RoadIndex.fromTiles(tiles(listOf(road)))).isEmpty())
    }

    @Test
    fun `a stretch away from every road is a gap`() {
        val gaps = GapFinder.find(ride(detour), RoadIndex.fromTiles(tiles(listOf(road)))) // detour off the map
        assertEquals(1, gaps.size)
        // Off the road (more than 25 m) from about 625 m to 1175 m east: the 400 m along y=100 and the
        // slopes either side.
        assertTrue("length ${gaps[0].lengthM}", gaps[0].lengthM in 450.0..600.0)
        assertTrue(gaps[0].startUtcMs < gaps[0].endUtcMs)
    }

    @Test
    fun `a gap needs 150 m`() {
        val short =
            listOf(at(0.0, 0.0), at(0.0, 600.0), at(60.0, 640.0), at(60.0, 700.0), at(0.0, 740.0), at(0.0, 2000.0))
        assertTrue(GapFinder.find(ride(short), RoadIndex.fromTiles(tiles(listOf(road)))).isEmpty())
    }

    @Test
    fun `the detour on a mapped road is not a gap`() {
        val other = listOf(at(100.0, 650.0), at(100.0, 1150.0))
        val roads = RoadIndex.fromTiles(tiles(listOf(road, other)))
        assertTrue(GapFinder.find(ride(detour), roads).isEmpty())
    }

    @Test
    fun `where there is no map data nothing is a gap`() {
        // The tiles hold only a far-away road: the ride is all unknown.
        val far = listOf(at(50_000.0, 0.0), at(50_000.0, 100.0))
        assertTrue(GapFinder.find(ride(detour), RoadIndex.fromTiles(tiles(listOf(far)))).isEmpty())
    }

    @Test
    fun `only the most detailed level counts`() {
        val l1 = RoadIndex.fromTiles(tiles(listOf(road), level = MapLevel.L1))
        assertTrue(GapFinder.find(ride(detour), l1).isEmpty()) // no L0 data: unknown everywhere
    }

    @Test
    fun `every provenance is classified, and both kinds of OSM tiles are used`() {
        Provenance.entries.forEach { RoadIndex.isOsm(it) } // compiles only if `when` covers them all
        for (p in listOf(Provenance.OSM_OPENFREEMAP, Provenance.OSM_PROTOMAPS_OFFLINE)) {
            assertTrue(RoadIndex.isOsm(p))
            assertEquals(1, GapFinder.find(ride(detour), RoadIndex.fromTiles(tiles(listOf(road), p))).size)
        }
    }

    @Test
    fun `missing fixes are never bridged`() {
        // The same detour, but the fixes stop for 30 s in the middle of it.
        val fixes = ride(detour)
        val cut = fixes.filterIndexed { i, _ -> i !in 70..100 }
        val gaps = GapFinder.find(cut, RoadIndex.fromTiles(tiles(listOf(road))))
        gaps.forEach { g ->
            g.points.zipWithNext().forEach { (a, b) ->
                assertTrue(RoadIndex.segmentDistanceM(a, b, b) < GapFinder.STEP_M + 1)
            }
        }
    }

    @Test
    fun `current OSM can remove or split a gap`() {
        val gap = GapFinder.find(ride(detour), RoadIndex.fromTiles(tiles(listOf(road))))[0]
        // OSM now has a track over the middle 200 m of the detour.
        val track = listOf(at(100.0, 800.0), at(100.0, 1000.0))
        val left = GapFinder.confirm(gap, RoadIndex.fromOsm(listOf(track), emptyList()))
        assertTrue(left.size in 1..2)
        assertTrue(left.all { it.lengthM >= GapFinder.MIN_GAP_M })
        assertTrue(left.sumOf { it.lengthM } < gap.lengthM)
        // ... or the whole of it.
        val all = listOf(at(100.0, 500.0), at(100.0, 1300.0), at(0.0, 1300.0), at(0.0, 500.0))
        assertTrue(GapFinder.confirm(gap, RoadIndex.fromOsm(listOf(all), emptyList())).isEmpty())
    }

    @Test
    fun `a car park is not a missing road`() {
        val gap = GapFinder.find(ride(detour), RoadIndex.fromTiles(tiles(listOf(road))))[0]
        val park = listOf(at(30.0, 550.0), at(30.0, 1250.0), at(150.0, 1250.0), at(150.0, 550.0), at(30.0, 550.0))
        assertTrue(
            GapFinder.confirm(gap, RoadIndex.fromOsm(emptyList(), listOf(park))).all { it.lengthM < gap.lengthM },
        )
    }
}
