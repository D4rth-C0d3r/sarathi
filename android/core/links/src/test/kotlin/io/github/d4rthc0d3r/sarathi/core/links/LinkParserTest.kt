package io.github.d4rthc0d3r.sarathi.core.links

import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** Documented formats that the owner's apps didn't produce, and the safety rules. */
class LinkParserTest {
    private val basilica = Coord(15.5007, 73.9117)

    @Test
    fun geoUris() {
        assertEquals(SharedLink.Place(LinkSource.GEO, at = basilica), LinkParser.parse("geo:15.5007,73.9117"))
        assertEquals(
            SharedLink.Place(LinkSource.GEO, name = "Bom Jesus", at = basilica),
            LinkParser.parse("geo:0,0?q=15.5007,73.9117(Bom%20Jesus)"),
        )
        val address = LinkParser.parse("geo:0,0?q=Old+Goa+Road,+Goa") as SharedLink.Place
        assertEquals("Old Goa Road, Goa", address.query)
        assertNull(address.at)
    }

    @Test
    fun googleApiLinks() {
        val route =
            LinkParser.parse(
                "https://www.google.com/maps/dir/?api=1&destination=Calangute+Beach&waypoints=Mapusa|15.5007,73.9117" +
                    "&travelmode=two-wheeler&destination_place_id=ChIJcal",
            ) as SharedLink.Route
        assertNull(route.origin) // no origin: the current position
        assertEquals(listOf("Mapusa", null), route.via.map { it.query })
        assertEquals(basilica, route.via[1].at)
        assertEquals("ChIJcal", route.destination.googlePlaceId)
        assertEquals(SharedMode.TWO_WHEELER, route.mode)

        val search =
            LinkParser.parse(
                "https://www.google.com/maps/search/?api=1&query=15.5007%2C73.9117",
            ) as SharedLink.Place
        assertEquals(basilica, search.at)
    }

    @Test
    fun walkingIsKeptForTheConfirmationScreenToExplain() {
        val r =
            LinkParser.parse(
                "https://www.google.com/maps/dir/?api=1&origin=Panaji&destination=Mapusa&travelmode=walking",
            )
        assertEquals(SharedMode.WALKING, (r as SharedLink.Route).mode)
        assertEquals("Panaji", r.origin?.query)
    }

    @Test
    fun osmDirectionsAndUnknownLinks() {
        val r = LinkParser.parse("https://www.openstreetmap.org/directions?route=15.4953%2C73.8371%3B15.5007%2C73.9117")
        assertEquals(basilica, (r as SharedLink.Route).destination.at)
        assertNull(LinkParser.parse("https://example.com/maps/place/x"))
        assertNull(LinkParser.parse("nothing to see here"))
    }

    @Test
    fun redirectsStayOnTheAllowListAndStopAfterFiveHops() =
        runTest {
            val short = SharedLink.Short(LinkSource.GOOGLE, "https://maps.app.goo.gl/abc")
            assertNull(Redirects.resolve(short, "") { "https://evil.example/maps/place/x" })
            var hops = 0
            assertNull(
                Redirects.resolve(short, "") {
                    hops++
                    "https://maps.app.goo.gl/again"
                },
            )
            assertEquals(Redirects.MAX_HOPS, hops)
        }
}
