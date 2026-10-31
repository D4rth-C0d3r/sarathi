package io.github.d4rthc0d3r.sarathi.core.maps

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class PlacesBookTest {
    private val day = 24L * 60 * 60 * 1000
    private val basilica =
        Place("Basilica of Bom Jesus", "Old Goa", LatLng(15.5009, 73.9116), PlaceSource.GOOGLE, "ChIJbasilica")
    private val fort = Place("Fort Aguada", "Candolim", LatLng(15.4920, 73.7737), PlaceSource.OSM, "W123")

    @Test
    fun recentsAreNewestFirstWithoutDuplicatesAndCapped() {
        var book = PlacesBook().addRecent(basilica, 0).addRecent(fort, 1).addRecent(basilica, 2)
        assertEquals(listOf(basilica, fort), book.recents.map { it.place })
        repeat(PlacesBook.MAX_RECENTS + 5) { book = book.addRecent(fort.copy(id = "W$it"), it.toLong()) }
        assertEquals(PlacesBook.MAX_RECENTS, book.recents.size)
    }

    @Test
    fun homeIsReplacedFavouritesAreKeptByTitle() {
        val book =
            PlacesBook()
                .save(Label.HOME, "Home", fort, 0)
                .save(Label.HOME, "Home", basilica, 0)
                .save(Label.FAVOURITE, "Church", basilica, 0)
                .save(Label.FAVOURITE, "Beach fort", fort, 0)
                .save(Label.FAVOURITE, "church", fort, 0)
        assertEquals(listOf(Label.HOME, Label.FAVOURITE, Label.FAVOURITE), book.saved.map { it.label })
        assertEquals(
            basilica,
            book.saved
                .first()
                .kept.place,
        )
        assertEquals(listOf("Beach fort", "church"), book.saved.drop(1).map { it.title })
    }

    @Test
    fun googleCoordinatesExpireAfter30DaysAndComeBackOnLookup() {
        val book = PlacesBook().addRecent(basilica, 0).addRecent(fort, 0).save(Label.WORK, "Work", basilica, 0)
        val fresh = book.expire(30 * day)
        assertEquals(
            basilica.at,
            fresh.recents
                .first { it.place.source == PlaceSource.GOOGLE }
                .place.at,
        )

        val old = book.expire(31 * day)
        val google = old.recents.first { it.place.source == PlaceSource.GOOGLE }.place
        assertNull(google.at)
        assertEquals("ChIJbasilica", google.id)
        assertTrue(PlacesBook.needsLookup(google))
        assertEquals(
            fort.at,
            old.recents
                .first { it.place.source == PlaceSource.OSM }
                .place.at,
        )
        assertNull(
            old.saved
                .single()
                .kept.place.at,
        )

        val looked = old.refreshed(basilica, 31 * day)
        assertEquals(
            basilica.at,
            looked.saved
                .single()
                .kept.place.at,
        )
        assertEquals(
            basilica.at,
            looked.recents
                .first { it.place.source == PlaceSource.GOOGLE }
                .place.at,
        )
    }
}
