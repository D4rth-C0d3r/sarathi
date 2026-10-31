package io.github.d4rthc0d3r.sarathi.maps.osm

import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.maps.SearchBackend
import org.junit.Assert.assertEquals
import org.junit.Test

class PhotonSearchTest {
    /** Recorded from photon.komoot.io on 2026-10-31 (a public landmark). */
    private val body = javaClass.getResource("/photon-bom-jesus.json")!!.readText()

    @Test
    fun parsesFeaturesIntoSuggestionsWithCoordinates() {
        val first = PhotonSearch.parse(body).first()
        assertEquals("Basilica of Bom Jesus", first.title)
        assertEquals("Panaji - Ribandar - Old Goa Road, Old Goa, Panaji, Goa, India", first.subtitle)
        assertEquals("R3569663", first.id)
        assertEquals(SearchBackend.OSM, first.backend)
        assertEquals(LatLng(15.5006961, 73.9117039), first.at)
    }

    @Test
    fun skipsFeaturesWithoutGeometryOrName() {
        val odd = """{"features":[{"properties":{"name":"x"}},{"properties":{},"geometry":{"coordinates":[1,2]}}]}"""
        assertEquals(emptyList<Any>(), PhotonSearch.parse(odd))
    }
}
