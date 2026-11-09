package io.github.d4rthc0d3r.sarathi.core.links

import kotlinx.coroutines.test.runTest
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.double
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import kotlin.math.cos
import kotlin.math.hypot

/** Every share recorded on the owner's phone (docs/fixtures/share-links.json) parses as expected. */
class SharedLinkFixturesTest {
    private val fixtures =
        Json
            .parseToJsonElement(File(System.getProperty("sarathi.shareLinks")).readText())
            .jsonObject["fixtures"]!!
            .jsonArray
            .map { it.jsonObject }

    @Test
    fun everyRecordedShareParses() =
        runTest {
            assertTrue(fixtures.size >= 11)
            fixtures.forEach { check(it) }
        }

    private suspend fun check(f: JsonObject) {
        val id = f.text("id")!!
        val text = f.text("shared_text")!!
        var hops = 0
        val parsed =
            when (val first = LinkParser.parse(text)) {
                is SharedLink.Short -> {
                    Redirects.resolve(first, text) {
                        hops++
                        f.text("resolved")
                    }
                }

                else -> {
                    first
                }
            }
        assertNotNull(id, parsed)
        val expect = f["expect"]!!.jsonObject
        val tolerance = expect["tolerance_m"]?.jsonPrimitive?.double ?: 1.0
        when (expect.text("kind")) {
            "place" -> place(id, parsed as SharedLink.Place, expect, tolerance)
            "route" -> route(id, parsed as SharedLink.Route, expect, tolerance)
            else -> error("$id: unknown kind")
        }
        if (f.text("resolved") != null && f.text("shared_text")!!.contains("goo.gl")) assertEquals(id, 1, hops)
    }

    private fun place(
        id: String,
        p: SharedLink.Place,
        expect: JsonObject,
        tolerance: Double,
    ) {
        assertEquals(id, expect.text("name"), p.name)
        expect.text("address")?.let { assertEquals(id, it, p.address) }
        expect.text("google_feature_id")?.let { assertEquals(id, it, p.googleFeatureId) }
        expect.text("mappls_pin")?.let { assertEquals(id, it, p.mapplsPin) }
        near(id, expect["coordinates"], p.at, tolerance)
        when (expect.text("needs")) {
            "search by name" -> assertEquals(id, p.name, p.query)
            "Mappls Pin lookup" -> assertNull(id, p.at)
        }
    }

    private fun route(
        id: String,
        r: SharedLink.Route,
        expect: JsonObject,
        tolerance: Double,
    ) {
        val mode = if (expect.text("travel_mode") == "two_wheeler") SharedMode.TWO_WHEELER else SharedMode.DRIVING
        assertEquals(id, mode, r.mode)
        val stops = expect["stops"]!!.jsonArray.map { it.jsonObject }
        assertEquals(id, stops.size, r.stops.size)
        stops.zip(r.stops).forEach { (e, s) ->
            assertEquals(id, e.text("name"), s.name)
            near(id, e["coordinates"], s.at, tolerance)
        }
    }

    private fun near(
        id: String,
        expected: JsonElement?,
        actual: Coord?,
        toleranceM: Double,
    ) {
        if (expected == null || expected is JsonNull) {
            assertNull(id, actual)
            return
        }
        val (lat, lng) = (expected as JsonArray).map { it.jsonPrimitive.double }
        assertNotNull(id, actual)
        val metres = hypot((actual!!.lat - lat) * M_PER_DEG, (actual.lng - lng) * M_PER_DEG * cos(Math.toRadians(lat)))
        assertTrue("$id: $actual is ${metres}m from ($lat, $lng)", metres <= toleranceM)
    }

    private fun JsonObject.text(key: String) =
        (this[key] as? JsonElement)
            ?.takeIf {
                it !is JsonNull
            }?.jsonPrimitive
            ?.contentOrNull

    private companion object {
        const val M_PER_DEG = 111_320.0
    }
}
