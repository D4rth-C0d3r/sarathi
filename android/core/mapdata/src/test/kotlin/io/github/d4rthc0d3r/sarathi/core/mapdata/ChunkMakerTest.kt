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
                val chunk = ChunkMaker.chunk(shapes, frame, EPOCH, DATA_VERSION)
                assertArrayEquals("$set/$name", bin.readBytes(), BinaryCodec.encode(chunk))
                compared++
            }
        }
        assertEquals(32, compared) // 9 Bengaluru, 23 expressway
    }

    @Test
    fun offlineChunksMatchTheReference() {
        val frame = RideFrame.near(15.49, 73.83)
        val offline = File(root, "protomaps-20261109")
        val expected = File(root, "chunks/panaji-offline").listFiles { f -> f.name.endsWith(".bin") }!!.sorted()
        for (bin in expected) {
            val name = bin.nameWithoutExtension
            val (z, x, y) = name.split("-").map { it.toInt() }
            val read = VectorTile.read(File(offline, "$name.pbf").readBytes(), MapSchema.Protomaps.layers)
            val shapes = ChunkMaker.shapes(read, MapLevel.forZoom(z)!!, x, y, MapSchema.Protomaps)
            val chunk = ChunkMaker.chunk(shapes, frame, EPOCH, OFFLINE_VERSION)
            assertEquals(Provenance.OSM_PROTOMAPS_OFFLINE, chunk.provenance)
            assertArrayEquals("panaji/$name", bin.readBytes(), BinaryCodec.encode(chunk))
        }
        assertEquals(10, expected.size)
    }

    @Test
    fun readsTilesFromAPmTilesRegion() {
        val file = java.io.RandomAccessFile(File(root, "panaji-20261109.pmtiles"), "r")
        file.use { raf ->
            val region =
                PmTiles { offset, length ->
                    ByteArray(length).also {
                        raf.seek(offset)
                        raf.readFully(it)
                    }
                }
            assertEquals(12, region.minZoom)
            assertEquals(14, region.maxZoom)
            for (zxy in listOf("14-11551-7478", "12-2887-1869")) {
                val (z, x, y) = zxy.split("-").map { it.toInt() }
                val tile = region.tile(z, x, y)!!
                val fixture = File(root, "protomaps-20261109/$zxy.pbf").readBytes()
                assertArrayEquals(zxy, fixture, VectorTile.filter(tile, MapSchema.Protomaps.layers))
            }
            assertEquals(null, region.tile(14, 11550, 7478)) // outside the extract
        }
    }

    @Test
    fun hilbertTileIds() {
        // PMTiles specification examples.
        assertEquals(0L, PmTiles.tileId(0, 0, 0))
        assertEquals(1L, PmTiles.tileId(1, 0, 0))
        assertEquals(2L, PmTiles.tileId(1, 0, 1))
        assertEquals(3L, PmTiles.tileId(1, 1, 1))
        assertEquals(4L, PmTiles.tileId(1, 1, 0))
        assertEquals(5L, PmTiles.tileId(2, 0, 0))
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
        const val OFFLINE_VERSION = 20261109L
    }
}
