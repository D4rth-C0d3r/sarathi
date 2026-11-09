package io.github.d4rthc0d3r.sarathi.core.links

import io.github.d4rthc0d3r.sarathi.core.links.SharedLink.Place

/** Google Maps links: `/maps/place/…`, `/maps/dir/…`, `/maps/search/…`, and the `api=1` forms. */
internal object GoogleLinks {
    private val HOST = Regex("""^(maps\.)?google\.[a-z.]+$""")
    private val FEATURE_ID = Regex("""!1s(0x[0-9a-f]+:0x[0-9a-f]+)""")

    fun parse(url: Url): SharedLink? {
        val seg = url.segments
        if (!HOST.matches(url.host) || seg.firstOrNull() != "maps") return null
        val api = url.query["api"] == "1"
        return when (seg.getOrNull(1)) {
            "dir" -> {
                if (api) apiRoute(url.query) else route(seg.drop(2))
            }

            "place" -> {
                if (api) query(url.query) else place(seg.drop(2))
            }

            "search" -> {
                if (api) {
                    query(
                        url.query,
                    )
                } else {
                    seg.getOrNull(2)?.let { Stops.stop(LinkSource.GOOGLE, Stops.decode(it)) }
                }
            }

            else -> {
                query(url.query)
            }
        }
    }

    private fun place(seg: List<String>): Place? {
        val name = seg.firstOrNull()?.takeIf { !it.startsWith("@") && !it.startsWith("data=") }?.let(Stops::decode)
        val data = seg.firstOrNull { it.startsWith("data=") }?.removePrefix("data=").orEmpty()
        // The place's own !3d/!4d, never the @ viewport (ADR-0008).
        val at = GoogleData.parse(data).firstNotNullOfOrNull { it.coord() }
        val feature = FEATURE_ID.find(data)?.groupValues?.get(1)
        return if (name == null && at == null) {
            null
        } else {
            Place(
                LinkSource.GOOGLE,
                name = name,
                at = at,
                googleFeatureId = feature,
                query = name.takeIf { at == null },
            )
        }
    }

    private fun route(seg: List<String>): SharedLink? {
        val names = seg.takeWhile { !it.startsWith("@") && !it.startsWith("data=") }.map(Stops::decode)
        if (names.size < 2) return null
        val data = GoogleData.parse(seg.firstOrNull { it.startsWith("data=") }?.removePrefix("data=").orEmpty())
        val coords = GoogleData.routeStops(data).takeIf { it.size == names.size }
        val stops = names.mapIndexed { i, name -> Stops.stop(LinkSource.GOOGLE, name, coords?.get(i)) }
        return Stops.route(LinkSource.GOOGLE, stops, GoogleData.mode(data) ?: SharedMode.UNKNOWN)
    }

    private fun apiRoute(q: Map<String, String>): SharedLink? {
        val destination = q["destination"]?.takeIf { it.isNotBlank() } ?: return null
        val waypoints = q["waypoints"]?.split('|')?.filter { it.isNotBlank() }.orEmpty()
        val waypointIds = q["waypoint_place_ids"]?.split('|').orEmpty()
        val stops =
            listOf(Stops.stop(LinkSource.GOOGLE, q["origin"].orEmpty(), placeId = q["origin_place_id"])) +
                waypoints.mapIndexed { i, w -> Stops.stop(LinkSource.GOOGLE, w, placeId = waypointIds.getOrNull(i)) } +
                Stops.stop(LinkSource.GOOGLE, destination, placeId = q["destination_place_id"])
        return Stops.route(LinkSource.GOOGLE, stops, apiMode(q["travelmode"]))
    }

    private fun query(q: Map<String, String>): Place? =
        (q["query"] ?: q["q"] ?: q["ll"])?.let { Stops.stop(LinkSource.GOOGLE, it, placeId = q["query_place_id"]) }

    private fun apiMode(mode: String?): SharedMode =
        when (mode) {
            "two-wheeler" -> SharedMode.TWO_WHEELER
            "driving" -> SharedMode.DRIVING
            "walking" -> SharedMode.WALKING
            "bicycling" -> SharedMode.BICYCLING
            "transit" -> SharedMode.TRANSIT
            else -> SharedMode.UNKNOWN
        }
}
