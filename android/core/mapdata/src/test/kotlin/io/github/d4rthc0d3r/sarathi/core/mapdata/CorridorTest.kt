package io.github.d4rthc0d3r.sarathi.core.mapdata

import io.github.d4rthc0d3r.sarathi.core.protocol.BinaryCodec
import io.github.d4rthc0d3r.sarathi.core.protocol.RideFrame
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class CorridorTest {
    // Panaji KTC to Calangute Beach, simplified: about 15 km north-west through Porvorim.
    private val route =
        listOf(
            GeoPoint(15.4989, 73.8278),
            GeoPoint(15.5100, 73.8310),
            GeoPoint(15.5400, 73.8160),
            GeoPoint(15.5600, 73.7900),
            GeoPoint(15.5440, 73.7550),
        )

    @Test
    fun theRidersTilesComeFirstThenAlongTheRoute() {
        val plan = Corridor.plan(route, route[0])
        val (x, y) = Corridor.tileAt(14, route[0])
        assertEquals(TileId(MapLevel.L0, x, y), plan.first())
        assertEquals(plan.size, plan.toSet().size) // no repeats
        // The destination is beyond the 10 km of L0 tiles, but its L1 tile is planned.
        val (dx, dy) = Corridor.tileAt(12, route.last())
        assertTrue(TileId(MapLevel.L1, dx, dy) in plan)
        // A tile along the way comes after the rider's.
        val (mx, my) = Corridor.tileAt(14, route[2])
        assertTrue(plan.indexOf(TileId(MapLevel.L0, mx, my)) > 0)
    }

    @Test
    fun withoutARouteOnlyAroundTheRider() {
        val plan = Corridor.plan(emptyList(), route[0])
        assertTrue(plan.isNotEmpty())
        assertTrue(plan.all { it.level == MapLevel.L0 || it.level == MapLevel.L1 })
        assertTrue(plan.count { it.level == MapLevel.L0 } <= 9) // 1.5 km around: at most 3x3 tiles here
    }

    @Test
    fun aDenseTileSplitsIntoQuartersThatHoldItsRoads() {
        val root = File(System.getProperty("sarathi.mapFixtures"))
        val read = VectorTile.read(File(root, "openfreemap-20261101/14-11722-7596.pbf").readBytes())
        val shapes = ChunkMaker.shapes(read, TileId(MapLevel.L0, 11722, 7596), MapSchema.OpenMapTiles, 1)
        val frame = RideFrame.near(12.97, 77.59)
        val whole = ChunkMaker.chunk(shapes, frame, 1)
        val quarters = ChunkMaker.quarters(shapes, frame, 1)
        assertEquals(4, quarters.size)
        assertEquals(
            setOf(23444 to 15192, 23445 to 15192, 23444 to 15193, 23445 to 15193),
            quarters.map { it.key.tileX.toInt() to it.key.tileY.toInt() }.toSet(),
        )
        assertTrue(quarters.all { it.key.level == 2 && it.quantumDm == whole.quantumDm })
        // Each is a valid message and smaller than the whole; together they hold at least its points.
        val sizes = quarters.map { BinaryCodec.encode(it).size }
        assertTrue(sizes.all { it < BinaryCodec.encode(whole).size })
        val points = { c: io.github.d4rthc0d3r.sarathi.core.protocol.Chunk -> c.lines.sumOf { it.points.size } }
        assertTrue(quarters.sumOf(points) >= points(whole))
    }
}
