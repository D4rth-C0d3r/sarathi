package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import io.github.d4rthc0d3r.sarathi.core.osmcontrib.TestMaps.at
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class OverpassTest {
    private val gap = Gap((0..40).map { at(100.0, 700.0 + it * 10.0) }, 400.0, 1, 2)

    @Test
    fun `the query asks for any highway and car parks around the gap`() {
        val q = Overpass.query(gap)
        assertTrue(q, q.startsWith("[out:xml]"))
        assertTrue(q.contains("way[highway](around:30,"))
        assertTrue(q.contains("way[amenity~\"^(parking|fuel)$\"](around:30,"))
        assertTrue(q.endsWith("out geom;"))
        // A point every 50 m and the end: 9 points, 18 numbers.
        val numbers = q.substringAfter("way[highway](around:30,").substringBefore(")").split(',')
        assertEquals(18, numbers.size)
    }

    @Test
    fun `reads roads and closed car park areas`() {
        val xml =
            """
            <?xml version="1.0" encoding="UTF-8"?>
            <osm version="0.6">
              <way id="1"><nd ref="1" lat="15.5" lon="73.8"/><nd ref="2" lat="15.5" lon="73.81"/>
                <tag k="highway" v="track"/></way>
              <way id="2"><nd ref="3" lat="15.6" lon="73.8"/><nd ref="4" lat="15.6" lon="73.801"/>
                <nd ref="5" lat="15.601" lon="73.801"/><nd ref="3" lat="15.6" lon="73.8"/>
                <tag k="amenity" v="parking"/></way>
              <way id="3"><nd ref="6" lat="15.7" lon="73.8"/><nd ref="7" lat="15.7" lon="73.81"/>
                <tag k="waterway" v="stream"/></way>
            </osm>
            """.trimIndent()
        val roads = Overpass.parse(xml.byteInputStream())
        assertTrue(roads.distanceM(at(0.0, 300.0)) < 1.0) // on the track
        assertTrue(
            roads.insideArea(
                io.github.d4rthc0d3r.sarathi.core.mapdata
                    .GeoPoint(15.6005, 73.8008),
            ),
        )
        assertFalse(
            roads.distanceM(
                io.github.d4rthc0d3r.sarathi.core.mapdata
                    .GeoPoint(15.7, 73.805),
            ) < 40,
        ) // a stream
    }

    @Test(expected = Exception::class)
    fun `a DOCTYPE is refused`() {
        val xml = """<?xml version="1.0"?><!DOCTYPE osm [<!ENTITY x SYSTEM "file:///etc/passwd">]><osm>&x;</osm>"""
        Overpass.parse(xml.byteInputStream())
    }
}
