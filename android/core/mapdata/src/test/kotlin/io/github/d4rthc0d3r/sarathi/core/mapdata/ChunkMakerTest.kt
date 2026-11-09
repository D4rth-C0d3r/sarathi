package io.github.d4rthc0d3r.sarathi.core.mapdata

import io.github.d4rthc0d3r.sarathi.core.protocol.BinaryCodec
import io.github.d4rthc0d3r.sarathi.core.protocol.Provenance
import io.github.d4rthc0d3r.sarathi.core.protocol.RideFrame
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * The recorded tiles in docs/fixtures/map, made into chunks here, must match the chunks
 * tools/make_chunks.py made from them, byte for byte (docs/fixtures/map/README.md).
 */
class ChunkMakerTest {
    private val root = File(System.getProperty("sarathi.mapFixtures"))
    private val tiles = File(root, "openfreemap-20261101")

    // The ride frames the fixtures were made in (README).
    private val sets =
        mapOf(
            "bengaluru" to (12.97 to 77.59),
            "expressway" to (18.75 to 73.40),
        )

    @Test
    fun everyRecordedChunkMatchesTheReference() {
        var compared = 0
        for ((set, origin) in sets) {
            val frame = RideFrame.near(origin.first, origin.second)
            val expected = File(root, "chunks/$set").listFiles { f -> f.name.endsWith(".bin") }!!.sorted()
            for (bin in expected) {
                val name = bin.nameWithoutExtension
                val (z, x, y) = name.split("-").map { it.toInt() }
                val level = MapLevel.forZoom(z)!!
                val shapes = ChunkMaker.shapes(VectorTile.read(File(tiles, "$name.pbf").readBytes()), level, x, y)
                val chunk = ChunkMaker.chunk(shapes, frame, EPOCH, DATA_VERSION, Provenance.OSM_OPENFREEMAP)
                assertArrayEquals("$set/$name", bin.readBytes(), BinaryCodec.encode(chunk))
                compared++
            }
        }
        assertEquals(32, compared) // 9 Bengaluru, 23 expressway
    }

    @Test
    fun filteringKeepsTheLayersByteForByte() {
        val tile = File(tiles, "14-11723-7596.pbf").readBytes()
        assertArrayEquals(tile, VectorTile.filter(tile)) // the fixtures are already filtered
        assertTrue(VectorTile.filter(tile, setOf("water")).size < tile.size)
        assertEquals(setOf("water"), VectorTile.read(VectorTile.filter(tile, setOf("water"))).keys)
    }

    @Test
    fun aDenseCityTileHasItsRoadsAndWater() {
        val read = VectorTile.read(File(tiles, "14-11723-7596.pbf").readBytes())
        val shapes = ChunkMaker.shapes(read, MapLevel.L0, 11723, 7596)
        assertTrue(shapes.lines.size > 500)
        assertTrue(shapes.polys.isNotEmpty())
        // L1 keeps only motorway to secondary.
        val l1 =
            ChunkMaker.shapes(
                VectorTile.read(File(tiles, "12-2882-1830.pbf").readBytes()),
                MapLevel.L1,
                2882,
                1830,
            )
        assertTrue(l1.lines.all { it.kind <= 3 })
    }

    private companion object {
        const val EPOCH = 1
        const val DATA_VERSION = 20261101L
    }
}
