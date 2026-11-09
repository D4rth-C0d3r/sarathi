package io.github.d4rthc0d3r.sarathi.core.links

data class Coord(
    val lat: Double,
    val lng: Double,
) {
    /** In range, and not geo:'s 0,0 placeholder. */
    val valid get() = lat in -MAX_LAT..MAX_LAT && lng in -MAX_LNG..MAX_LNG && !(lat == 0.0 && lng == 0.0)

    private companion object {
        const val MAX_LAT = 90.0
        const val MAX_LNG = 180.0
    }
}

/** Travel modes a shared route can carry; only two-wheeler and driving suit a ride (ADR-0008). */
enum class SharedMode { TWO_WHEELER, DRIVING, WALKING, BICYCLING, TRANSIT, UNKNOWN }

/** Which app the link came from: Google content has storage rules of its own (ADR-0008). */
enum class LinkSource { GOOGLE, MAPPLS, OSM, ORGANIC_MAPS, GEO, TEXT }

/** What a shared link or text says, before anything is looked up. */
sealed interface SharedLink {
    val source: LinkSource

    /**
     * A place. Whatever is known: coordinates, a name or address to search for, a Google place ID,
     * or a Mappls Pin.
     */
    data class Place(
        override val source: LinkSource,
        val name: String? = null,
        val address: String? = null,
        val at: Coord? = null,
        val googlePlaceId: String? = null,
        /** Google's internal feature ID (0x…:0x…): not usable with the Places API, kept for logs. */
        val googleFeatureId: String? = null,
        val mapplsPin: String? = null,
        /** Text to search for when there's nothing better. */
        val query: String? = null,
    ) : SharedLink

    /** A route; a null [origin] means the rider's current position. */
    data class Route(
        override val source: LinkSource,
        val origin: Place?,
        val via: List<Place>,
        val destination: Place,
        val mode: SharedMode,
    ) : SharedLink {
        val stops get() = listOfNotNull(origin) + via + destination
    }

    /** A short link: its redirects say where it points ([Redirects]). */
    data class Short(
        override val source: LinkSource,
        val url: String,
    ) : SharedLink
}
