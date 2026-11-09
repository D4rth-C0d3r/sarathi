package io.github.d4rthc0d3r.sarathi.core.links

import io.github.d4rthc0d3r.sarathi.core.links.SharedLink.Place

/** Mappls, OSM, Organic Maps and geo: links, and plain coordinates in text. */
internal object OtherLinks {
    private val MAPPLS_PIN = Regex("""^[A-Za-z0-9]{6}$""")
    private val COORDS_IN_TEXT = Regex("""(-?\d{1,2}\.\d{3,})\s*,\s*(-?\d{1,3}\.\d{3,})""")
    private const val MAP_PARTS = 3 // #map=<zoom>/<lat>/<lon>

    /** Mappls: `/<Mappls Pin>` with the name and address on the lines after the link, or `/pom/<lat>,<lng>`. */
    fun mappls(
        url: Url,
        link: String,
        text: String,
    ): Place? {
        val seg = url.segments.filter { it.isNotEmpty() }
        val pin = seg.singleOrNull()?.takeIf { MAPPLS_PIN.matches(it) }
        return when {
            seg.firstOrNull() == "pom" -> {
                seg.getOrNull(1)?.let { Stops.coords(Stops.decode(it)) }?.let { Place(LinkSource.MAPPLS, at = it) }
            }

            pin != null -> {
                val after =
                    text
                        .lines()
                        .dropWhile { !it.contains(link) }
                        .drop(1)
                        .map { it.trim() }
                        .filter { it.isNotEmpty() }
                Place(
                    LinkSource.MAPPLS,
                    name = after.firstOrNull(),
                    address =
                        after
                            .drop(
                                1,
                            ).joinToString(", ")
                            .ifEmpty {
                                null
                            },
                    mapplsPin = pin,
                )
            }

            else -> {
                null
            }
        }
    }

    fun osm(url: Url): SharedLink? {
        val seg = url.segments
        return when (seg.firstOrNull()) {
            "go" -> {
                seg.getOrNull(1)?.let(ShortCodes::osm)?.let { Place(LinkSource.OSM, at = it) }
            }

            "directions" -> {
                val stops =
                    url.query["route"]
                        ?.split(';')
                        ?.mapNotNull(Stops::coords)
                        .orEmpty()
                stops.takeIf { it.size >= 2 }?.let { s ->
                    Stops.route(LinkSource.OSM, s.map { Place(LinkSource.OSM, at = it) }, SharedMode.UNKNOWN)
                }
            }

            else -> {
                (marker(url) ?: view(url))?.let { Place(LinkSource.OSM, at = it) }
            }
        }
    }

    private fun marker(url: Url): Coord? =
        url.query["mlat"]?.let { lat ->
            url.query["mlon"]?.let {
                Stops.coords("$lat,$it")
            }
        }

    private fun view(url: Url): Coord? =
        url.fragment
            .split('&')
            .firstOrNull { it.startsWith("map=") }
            ?.removePrefix("map=")
            ?.split('/')
            ?.takeIf { it.size == MAP_PARTS }
            ?.let { Stops.coords("${it[1]},${it[2]}") }

    /** Organic Maps: `/<ge0 code>/<Name_with_underscores>`. */
    fun organic(url: Url): Place? {
        val seg = url.segments
        val name =
            seg
                .getOrNull(1)
                ?.let(Stops::decode)
                ?.replace('_', ' ')
                ?.trim()
                ?.ifEmpty { null }
        return seg.firstOrNull()?.let(ShortCodes::ge0)?.let { Place(LinkSource.ORGANIC_MAPS, name = name, at = it) }
    }

    /** `geo:<lat>,<lng>`, `geo:<lat>,<lng>?q=<lat>,<lng>(<label>)` and `geo:0,0?q=<address>`. */
    fun geo(rest: String): Place? {
        val at = Stops.coords(rest.substringBefore('?').substringBefore(';'))
        val q =
            rest
                .substringAfter('?', "")
                .split('&')
                .firstOrNull { it.startsWith("q=") }
                ?.let { Stops.decode(it.removePrefix("q=")).trim() }
        val label =
            q
                ?.substringAfter('(', "")
                ?.substringBeforeLast(')', "")
                ?.trim()
                ?.ifEmpty { null }
        val qAt = q?.let { Stops.coords(it.substringBefore('(')) }
        return when {
            q.isNullOrEmpty() -> at?.let { Place(LinkSource.GEO, at = it) }
            qAt != null -> Place(LinkSource.GEO, name = label, at = qAt)
            else -> Place(LinkSource.GEO, name = q, query = q)
        }
    }

    fun coordinatesInText(text: String): Place? =
        COORDS_IN_TEXT.find(text)?.let { m -> Stops.coords("${m.groupValues[1]},${m.groupValues[2]}") }?.let { at ->
            val name =
                text.lines().map { it.trim() }.firstOrNull {
                    it.isNotEmpty() &&
                        !COORDS_IN_TEXT.containsMatchIn(it)
                }
            Place(LinkSource.TEXT, name = name, at = at)
        }
}
