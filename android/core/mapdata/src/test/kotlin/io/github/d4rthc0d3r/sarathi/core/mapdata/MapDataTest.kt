package io.github.d4rthc0d3r.sarathi.core.mapdata

import io.github.d4rthc0d3r.sarathi.core.protocol.BinaryCodec
import io.github.d4rthc0d3r.sarathi.core.protocol.Provenance
import io.github.d4rthc0d3r.sarathi.core.protocol.RideFrame
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile

class MapDataTest {
    @get:Rule val temp = TemporaryFolder()

    private val root = File(System.getProperty("sarathi.mapFixtures"))

    /** OpenFreeMap from the recorded tiles; [reachable] false acts as no signal. */
    private inner class Online : TileSource {
        var reachable = true
        var fetched = 0
        override val schema = MapSchema.OpenMapTiles

        override suspend fun dataVersion(): Long? = if (reachable) 20261101L else throw IOException("offline")

        override suspend fun tile(
            z: Int,
            x: Int,
            y: Int,
        ): ByteArray? {
            if (!reachable) throw IOException("offline")
            fetched++
            return File(root, "openfreemap-20261101/$z-$x-$y.pbf").takeIf { it.exists() }?.readBytes()
        }
    }

    /** The offline region from the PMTiles fixture. */
    private inner class Offline : TileSource {
        private val raf = RandomAccessFile(File(root, "panaji-20261109.pmtiles"), "r")
        private val region =
            PmTiles { offset, length ->
                ByteArray(length).also {
                    raf.seek(offset)
                    raf.readFully(it)
                }
            }
        override val schema = MapSchema.Protomaps

        override suspend fun dataVersion() = 20261109L

        override suspend fun tile(
            z: Int,
            x: Int,
            y: Int,
        ) = region.tile(z, x, y)
    }

    @Test
    fun onlineFirstThenFromTheCache() =
        runTest {
            val online = Online()
            val data = MapData(TileCache(temp.newFolder()), online, Offline())
            val first = data.shapes(TileId(MapLevel.L0, 11723, 7596))!!
            assertEquals(TileOrigin.ONLINE, first.origin)
            val again = data.shapes(TileId(MapLevel.L0, 11723, 7596))!!
            assertEquals(TileOrigin.CACHE, again.origin)
            assertEquals(1, online.fetched)
            // The cached tile makes exactly the chunk the fresh one did.
            val frame = RideFrame.near(12.97, 77.59)
            assertArrayEquals(
                BinaryCodec.encode(ChunkMaker.chunk(first.shapes, frame, 1)),
                BinaryCodec.encode(ChunkMaker.chunk(again.shapes, frame, 1)),
            )
        }

    @Test
    fun withoutSignalAnOlderCacheThenTheOfflineRegion() =
        runTest {
            val online = Online()
            val data = MapData(TileCache(temp.newFolder()), online, Offline())
            assertEquals(TileOrigin.ONLINE, data.shapes(TileId(MapLevel.L0, 11723, 7596))!!.origin) // prefetched
            online.reachable = false
            assertEquals(TileOrigin.CACHE_OLDER, data.shapes(TileId(MapLevel.L0, 11723, 7596))!!.origin)
            val panaji = data.shapes(TileId(MapLevel.L0, 11551, 7478))!!
            assertEquals(TileOrigin.OFFLINE, panaji.origin)
            assertEquals(Provenance.OSM_PROTOMAPS_OFFLINE, panaji.shapes.provenance)
            assertEquals(20261109L, panaji.shapes.dataVersion)
            assertNull(data.shapes(TileId(MapLevel.L0, 1, 1))) // nowhere
        }

    @Test
    fun cacheDropsTheLeastRecentlyUsed() {
        var clock = 1_000_000L
        val dir = temp.newFolder()
        val read = VectorTile.read(File(root, "openfreemap-20261101/14-11723-7596.pbf").readBytes())
        val shapes = { x: Int -> ChunkMaker.shapes(read, TileId(MapLevel.L0, x, 7596), MapSchema.OpenMapTiles, 1) }
        val probe = TileCache(dir)
        probe.put(shapes(1))
        val one = probe.size()
        File(dir, "L0").deleteRecursively()
        // Room for about three tiles.
        val cache = TileCache(dir, capBytes = one * 3 + one / 2) { clock }
        for (x in 1..3) {
            clock += 1000
            cache.put(shapes(x))
        }
        clock += 1000
        val key = { x: Int -> TileKey(TileId(MapLevel.L0, x, 7596), 1, Provenance.OSM_OPENFREEMAP) }
        assertNotNull(cache.get(key(1))) // 1 is fresh again
        clock += 1000
        cache.put(shapes(4)) // over the cap: 2, the least recently used, goes
        assertNull(cache.get(key(2)))
        assertNotNull(cache.get(key(1)))
        assertTrue(cache.size() <= one * 3 + one / 2)
    }

    @Test
    fun aDamagedFileIsDroppedNotFatal() {
        val dir = temp.newFolder()
        val cache = TileCache(dir)
        val read = VectorTile.read(File(root, "openfreemap-20261101/14-11723-7596.pbf").readBytes())
        cache.put(ChunkMaker.shapes(read, TileId(MapLevel.L0, 5, 5), MapSchema.OpenMapTiles, 1))
        val file = dir.walkTopDown().first { it.isFile }
        file.writeBytes(byteArrayOf(1, 2, 3))
        assertNull(cache.get(TileKey(TileId(MapLevel.L0, 5, 5), 1, Provenance.OSM_OPENFREEMAP)))
        assertTrue(!file.exists())
    }
}
