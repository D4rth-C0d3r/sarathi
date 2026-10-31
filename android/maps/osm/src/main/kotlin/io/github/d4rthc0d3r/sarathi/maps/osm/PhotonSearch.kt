package io.github.d4rthc0d3r.sarathi.maps.osm

import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.maps.Place
import io.github.d4rthc0d3r.sarathi.core.maps.PlaceSource
import io.github.d4rthc0d3r.sarathi.core.maps.SearchBackend
import io.github.d4rthc0d3r.sarathi.core.maps.SearchProvider
import io.github.d4rthc0d3r.sarathi.core.maps.SearchSession
import io.github.d4rthc0d3r.sarathi.core.maps.Suggestion
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.doubleOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import okhttp3.HttpUrl
import okhttp3.HttpUrl.Companion.toHttpUrl
import okhttp3.OkHttpClient

/**
 * OSM search through Photon (photon.komoot.io): free, fair use, no key. The search screen's
 * 300 ms pause keeps it to at most one request per 300 ms (ADR-0008).
 */
class PhotonSearch(
    private val http: OkHttpClient,
    private val userAgent: String,
    private val baseUrl: String = DEFAULT_URL,
) : SearchProvider {
    override val backend = SearchBackend.OSM

    override fun unavailable(): Unavailable? = null

    override suspend fun autocomplete(
        query: String,
        near: LatLng?,
        session: SearchSession,
    ): List<Suggestion> {
        val url =
            "$baseUrl/api/".toHttpUrl().newBuilder().apply {
                addQueryParameter("q", query)
                addQueryParameter("limit", LIMIT.toString())
                addQueryParameter("lang", "en")
                near?.let { at(it) }
            }
        return parse(http.getText(url.build(), userAgent))
    }

    override suspend fun details(
        suggestion: Suggestion,
        session: SearchSession,
    ): Place {
        // Photon's suggestions carry their coordinates; SearchService doesn't call this for them.
        val at = suggestion.at ?: error("Photon suggestion without coordinates")
        return Place(suggestion.title, suggestion.subtitle, at, PlaceSource.OSM, suggestion.id)
    }

    override suspend fun reverseGeocode(at: LatLng): Place? {
        val url = "$baseUrl/reverse".toHttpUrl().newBuilder().apply { at(at) }
        val s = parse(http.getText(url.build(), userAgent)).firstOrNull() ?: return null
        return Place(s.title, s.subtitle, at, PlaceSource.OSM, s.id)
    }

    private fun HttpUrl.Builder.at(at: LatLng) {
        addQueryParameter("lat", at.lat.toString())
        addQueryParameter("lon", at.lng.toString())
    }

    companion object {
        const val DEFAULT_URL = "https://photon.komoot.io"
        private const val LIMIT = 8
        private val JSON = Json { ignoreUnknownKeys = true }

        /** Photon's GeoJSON features as suggestions with coordinates. */
        fun parse(body: String): List<Suggestion> =
            JSON
                .parseToJsonElement(body)
                .jsonObject["features"]
                ?.jsonArray
                .orEmpty()
                .mapNotNull { suggestion(it.jsonObject) }

        private fun suggestion(feature: JsonObject): Suggestion? {
            val p = feature["properties"]?.jsonObject
            val at =
                feature["geometry"]
                    ?.jsonObject
                    ?.get("coordinates")
                    ?.jsonArray
                    ?.let(::point)
            if (p == null || at == null) return null

            fun text(key: String) = p[key]?.jsonPrimitive?.contentOrNull?.takeIf { it.isNotBlank() }
            val street = listOfNotNull(text("housenumber"), text("street")).joinToString(" ").ifEmpty { null }
            return (text("name") ?: street ?: text("city"))?.let { title ->
                val area = listOf("district", "city", "state", "country").mapNotNull(::text)
                val subtitle = (listOfNotNull(street.takeIf { it != title }) + area).distinct().joinToString(", ")
                val id = listOfNotNull(text("osm_type"), text("osm_id")).joinToString("")
                Suggestion(title, subtitle, SearchBackend.OSM, id, at)
            }
        }

        /** GeoJSON's [lng, lat]. */
        private fun point(coords: JsonArray): LatLng? {
            val lng = coords.getOrNull(0)?.jsonPrimitive?.doubleOrNull
            val lat = coords.getOrNull(1)?.jsonPrimitive?.doubleOrNull
            return if (lat != null && lng != null) LatLng(lat, lng) else null
        }
    }
}
