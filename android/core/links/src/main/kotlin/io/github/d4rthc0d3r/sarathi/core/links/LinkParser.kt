package io.github.d4rthc0d3r.sarathi.core.links

/**
 * Reads shared text or a link (ADR-0008, formats in `docs/fixtures/share-links.json`). Takes the
 * first supported link in the text; without one, plain coordinates in the text. Never fetches
 * anything: short links come back as [SharedLink.Short] for [Redirects].
 */
object LinkParser {
    private val LINK = Regex("""(?i)(?:https?://|geo:)[^\s<>"]+""")

    fun parse(text: String): SharedLink? =
        LINK.findAll(text).firstNotNullOfOrNull { parseUrl(it.value.trimEnd('.', ',', ';'), text) }
            ?: OtherLinks.coordinatesInText(text)

    /** One link; [text] is the whole shared text, for names some apps put around the link. */
    fun parseUrl(
        url: String,
        text: String = url,
    ): SharedLink? {
        if (url.startsWith("geo:", ignoreCase = true)) return OtherLinks.geo(url.substring("geo:".length))
        return Url.of(url)?.let { parts ->
            when (parts.host) {
                "maps.app.goo.gl" -> SharedLink.Short(LinkSource.GOOGLE, url)
                "goo.gl" -> SharedLink.Short(LinkSource.GOOGLE, url).takeIf { parts.path.startsWith("/maps") }
                "mappls.com", "maps.mappls.com" -> OtherLinks.mappls(parts, url, text)
                "openstreetmap.org", "osm.org" -> OtherLinks.osm(parts)
                "omaps.app", "ge0.me" -> OtherLinks.organic(parts)
                else -> GoogleLinks.parse(parts)
            }
        }
    }
}

/** A link split into decoded pieces; `www.` dropped from the host. */
internal class Url(
    val host: String,
    val path: String,
    val query: Map<String, String>,
    val fragment: String,
) {
    val segments get() = path.split('/').drop(1)

    companion object {
        private val PARTS = Regex("""(?i)^https?://([^/?#]+)([^?#]*)(?:\?([^#]*))?(?:#(.*))?$""")

        fun of(url: String): Url? =
            PARTS.matchEntire(url)?.groupValues?.let { g ->
                val params =
                    g[QUERY].split('&').filter { it.isNotEmpty() }.associate {
                        val kv = it.split('=', limit = 2)
                        Stops.decode(kv[0]) to Stops.decode(kv.getOrElse(1) { "" })
                    }
                val host = g[HOST].lowercase().substringBefore(':').removePrefix("www.")
                Url(host, g[PATH], params, Stops.decode(g[FRAGMENT]))
            }

        private const val HOST = 1
        private const val PATH = 2
        private const val QUERY = 3
        private const val FRAGMENT = 4
    }
}

/** Helpers shared by the link formats: stops given as text, routes, coordinates. */
internal object Stops {
    private val COORDS = Regex("""^\s*(-?\d{1,2}(?:\.\d+)?)\s*,\s*(-?\d{1,3}(?:\.\d+)?)\s*$""")
    private val CURRENT = setOf("", "your location", "my location", "current location")

    /** A stop given as text: coordinates, the current position, or a name to search for. */
    fun stop(
        source: LinkSource,
        text: String,
        at: Coord? = null,
        placeId: String? = null,
    ): SharedLink.Place {
        val trimmed = text.trim()
        val own = at ?: coords(trimmed)
        val isName = own == null && trimmed.lowercase() !in CURRENT
        return SharedLink.Place(
            source,
            name = trimmed.takeIf { isName || (at != null && trimmed.isNotEmpty()) },
            at = own,
            googlePlaceId = placeId?.takeIf { it.isNotBlank() },
            query = trimmed.takeIf { isName },
        )
    }

    /** A first stop that is only "your location" is the current position: a null origin. */
    fun route(
        source: LinkSource,
        stops: List<SharedLink.Place>,
        mode: SharedMode,
    ): SharedLink.Route {
        val first = stops.first()
        val current = first.at == null && first.query == null && first.googlePlaceId == null
        return SharedLink.Route(source, first.takeUnless { current }, stops.drop(1).dropLast(1), stops.last(), mode)
    }

    fun coords(text: String): Coord? =
        COORDS.matchEntire(text)?.let { m ->
            val lat = m.groupValues[1].toDoubleOrNull()
            val lng = m.groupValues[2].toDoubleOrNull()
            if (lat != null && lng != null) Coord(lat, lng).takeIf { it.valid } else null
        }

    fun decode(s: String): String = runCatching { java.net.URLDecoder.decode(s, Charsets.UTF_8) }.getOrDefault(s)
}
