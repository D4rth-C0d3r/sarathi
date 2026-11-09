package io.github.d4rthc0d3r.sarathi.maps.osm

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.util.zip.GZIPOutputStream

class OpenFreeMapTilesTest {
    @Test
    fun readsTheTemplateAndBuildDate() {
        // Trimmed from https://tiles.openfreemap.org/planet, 2026-11-09.
        val json =
            """{"tilejson":"3.0.0","tiles":""" +
                """["https://tiles.openfreemap.org/planet/20261101_080013_pt/{z}/{x}/{y}.pbf"],"vector_layers":[]}"""
        val (template, version) = OpenFreeMapTiles.parseTileJson(json)
        assertEquals("https://tiles.openfreemap.org/planet/20261101_080013_pt/{z}/{x}/{y}.pbf", template)
        assertEquals(20261101L, version)
    }

    @Test(expected = IOException::class)
    fun aTileJsonWithoutTilesIsAnError() {
        OpenFreeMapTiles.parseTileJson("""{"tilejson":"3.0.0"}""")
    }

    @Test
    fun gunzipsOnlyGzippedTiles() {
        val tile = byteArrayOf(0x1a, 0x05, 1, 2, 3)
        val zipped = ByteArrayOutputStream().also { out -> GZIPOutputStream(out).use { it.write(tile) } }.toByteArray()
        assertArrayEquals(tile, OpenFreeMapTiles.gunzipIfNeeded(zipped))
        assertArrayEquals(tile, OpenFreeMapTiles.gunzipIfNeeded(tile))
    }
}
