package io.github.d4rthc0d3r.sarathi.feature.destination

import io.github.d4rthc0d3r.sarathi.core.links.Coord
import io.github.d4rthc0d3r.sarathi.core.links.LinkParser
import io.github.d4rthc0d3r.sarathi.core.links.LinkSource
import io.github.d4rthc0d3r.sarathi.core.links.Redirects
import io.github.d4rthc0d3r.sarathi.core.links.SharedLink
import io.github.d4rthc0d3r.sarathi.core.links.SharedMode
import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.maps.Place
import io.github.d4rthc0d3r.sarathi.core.maps.PlaceSource
import io.github.d4rthc0d3r.sarathi.core.maps.ProviderException
import io.github.d4rthc0d3r.sarathi.core.maps.SearchBackend
import io.github.d4rthc0d3r.sarathi.core.maps.SearchService
import io.github.d4rthc0d3r.sarathi.core.maps.SearchSession
import io.github.d4rthc0d3r.sarathi.core.maps.Suggestion
import io.github.d4rthc0d3r.sarathi.core.maps.TravelMode
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable
import java.util.Locale
import java.util.UUID

/** A route shared from another app; a null [origin] is the rider's current position. */
data class SharedTrip(
    val origin: Place?,
    val via: List<Place>,
    val destination: Place,
    /** Always ridden two-wheeler where the backend can (ADR-0008); [shared] is what the link said. */
    val mode: TravelMode,
    val shared: SharedMode,
) {
    /** Walking, cycling and transit aren't for riding: routed as two-wheeler, with a note. */
    val unsupportedMode get() = shared in setOf(SharedMode.WALKING, SharedMode.BICYCLING, SharedMode.TRANSIT)
}

enum class LinkProblem { NOT_SUPPORTED, NOT_RESOLVED, NOT_FOUND }

class LinkException(
    val problem: LinkProblem,
    val reason: Unavailable? = null,
    cause: Throwable? = null,
) : Exception(problem.name, cause)

/**
 * Shared text or a link to a place or a route (ADR-0008): coordinates as they are, Mappls Pins
 * with Mappls, names through search. [location] fetches one redirect hop's `Location` header.
 */
class SharedLinkResolver(
    private val search: SearchService,
    private val near: () -> LatLng?,
    private val location: suspend (String) -> String?,
    /** What happened to a link, for the log; never the shared text itself. */
    private val log: (String) -> Unit = {},
) {
    sealed interface Result {
        data class One(
            val place: Place,
        ) : Result

        data class Trip(
            val trip: SharedTrip,
        ) : Result
    }

    suspend fun resolve(text: String): Result {
        val parsed = parse(text)
        return when (parsed) {
            is SharedLink.Place -> {
                Result.One(place(parsed))
            }

            is SharedLink.Route -> {
                Result.Trip(
                    SharedTrip(
                        parsed.origin?.let { place(it) },
                        parsed.via.map { place(it) },
                        place(parsed.destination),
                        TravelMode.TWO_WHEELER,
                        parsed.mode,
                    ),
                )
            }

            is SharedLink.Short -> {
                throw LinkException(LinkProblem.NOT_RESOLVED)
            }
        }
    }

    /** The link in [text], short links followed by their redirects. */
    private suspend fun parse(text: String): SharedLink {
        val first = LinkParser.parse(text) ?: throw LinkException(LinkProblem.NOT_SUPPORTED)
        val parsed = if (first is SharedLink.Short) Redirects.resolve(first, text, location) else first
        return parsed ?: throw LinkException(LinkProblem.NOT_RESOLVED)
    }

    private suspend fun place(p: SharedLink.Place): Place {
        val at = p.at
        val placeId = p.googlePlaceId
        val pin = p.mapplsPin
        return when {
            placeId != null -> {
                lookup(
                    Suggestion(p.name.orEmpty(), p.address.orEmpty(), SearchBackend.GOOGLE, placeId),
                )
            }

            at != null -> {
                named(p, LatLng(at.lat, at.lng))
            }

            pin != null -> {
                pin(p, pin)
            }

            else -> {
                byName(p.query ?: p.name ?: throw LinkException(LinkProblem.NOT_FOUND))
            }
        }
    }

    /** Coordinates from the link; a name from the link, else from reverse geocoding. */
    private suspend fun named(
        p: SharedLink.Place,
        at: LatLng,
    ): Place {
        // Google's coordinates fall under its 30-day rule even when they come in a link.
        val source = if (p.source == LinkSource.GOOGLE) PlaceSource.GOOGLE else PlaceSource.SHARED
        val name = p.name ?: reverse(at)
        return Place(name ?: formatted(Coord(at.lat, at.lng)), p.address ?: "", at, source)
    }

    private suspend fun reverse(at: LatLng): String? {
        val result = search.reverseGeocode(at)
        result.skipped.forEach { log("reverse geocode: ${it.backend} skipped: ${it.reason} ${it.detail.orEmpty()}") }
        return result.value?.name?.takeIf { it.isNotBlank() }
    }

    private suspend fun pin(
        p: SharedLink.Place,
        pin: String,
    ): Place =
        try {
            // Pins are case-insensitive, but Mappls's API knows them in upper case.
            lookup(Suggestion(p.name ?: pin, p.address.orEmpty(), SearchBackend.MAPPLS, pin.uppercase()))
        } catch (e: LinkException) {
            log("Mappls Pin lookup failed: ${e.reason} ${e.cause?.message.orEmpty()}")
            // Mappls can't be used (no key, over quota): the name the link came with, if any.
            // The name alone: with the address appended, search ranks unrelated places first.
            p.name?.let { byName(it) } ?: throw e
        }

    suspend fun byName(query: String): Place =
        try {
            search.findByName(query, near(), newSession()) ?: throw LinkException(LinkProblem.NOT_FOUND)
        } catch (e: ProviderException) {
            throw LinkException(LinkProblem.NOT_FOUND, e.reason, e)
        }

    private suspend fun lookup(s: Suggestion): Place =
        try {
            search.details(s, newSession())
        } catch (e: ProviderException) {
            throw LinkException(LinkProblem.NOT_FOUND, e.reason, e)
        }

    private fun newSession() = SearchSession(UUID.randomUUID().toString())

    companion object {
        fun formatted(c: Coord): String = String.format(Locale.ROOT, "%.5f, %.5f", c.lat, c.lng)
    }
}
