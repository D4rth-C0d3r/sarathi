package io.github.d4rthc0d3r.sarathi.maps.osm

import io.github.d4rthc0d3r.sarathi.core.maps.Polyline
import io.github.d4rthc0d3r.sarathi.core.maps.ProviderException
import io.github.d4rthc0d3r.sarathi.core.maps.Route
import io.github.d4rthc0d3r.sarathi.core.maps.RouteRequest
import io.github.d4rthc0d3r.sarathi.core.maps.RouteStep
import io.github.d4rthc0d3r.sarathi.core.maps.Routes
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingProvider
import io.github.d4rthc0d3r.sarathi.core.maps.StepManeuver
import io.github.d4rthc0d3r.sarathi.core.maps.TravelMode
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.add
import kotlinx.serialization.json.buildJsonArray
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.double
import kotlinx.serialization.json.doubleOrNull
import kotlinx.serialization.json.int
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.put
import okhttp3.HttpUrl.Companion.toHttpUrl
import okhttp3.OkHttpClient

/**
 * OSM online routing with GraphHopper's API (ADR-0006). The free plan (non-commercial, credits a
 * day) has only car, bike and foot profiles and at most 5 points a request, so routes are car
 * routes and say so ([Route.mode] is driving).
 */
class GraphHopperRouting(
    private val http: OkHttpClient,
    private val userAgent: String,
    private val apiKey: String?,
) : RoutingProvider {
    override val backend = RoutingBackend.OSM_GRAPHHOPPER

    override fun unavailable(): Unavailable? = if (apiKey.isNullOrEmpty()) Unavailable.NO_KEY else null

    override suspend fun route(request: RouteRequest): Route {
        val points = listOf(request.origin) + request.stops
        if (points.size >
            MAX_POINTS
        ) {
            throw ProviderException(Unavailable.NO_ROUTE, "GraphHopper's free plan: at most $MAX_POINTS points")
        }
        val url =
            URL
                .toHttpUrl()
                .newBuilder()
                .addQueryParameter("key", apiKey.orEmpty())
                .build()
        val body =
            buildJsonObject {
                put(
                    "points",
                    buildJsonArray {
                        points.forEach {
                            add(
                                buildJsonArray {
                                    add(it.lng)
                                    add(it.lat)
                                },
                            )
                        }
                    },
                )
                put("profile", PROFILE)
                put("locale", "en")
                put("instructions", true)
                put("points_encoded", true)
            }
        val result = http.postJson(url, userAgent, body.toString())
        return if (result.ok) parse(result.body) else throw failure(result)
    }

    companion object {
        const val MAX_POINTS = 5
        const val PROFILE = "car"
        const val ATTRIBUTION = "© OpenStreetMap contributors · GraphHopper"
        private const val URL = "https://graphhopper.com/api/1/route"
        private const val HTTP_BAD_REQUEST = 400
        private const val HTTP_UNAUTHORIZED = 401
        private const val HTTP_FORBIDDEN = 403
        private const val MS_PER_S = 1000
        private const val DEFAULT_MULTIPLIER = 1e5
        private val JSON = Json { ignoreUnknownKeys = true }

        private fun failure(result: HttpResult): ProviderException {
            val reason =
                when (result.code) {
                    HTTP_BAD_REQUEST -> Unavailable.NO_ROUTE

                    // "Cannot find point", "Connection between locations not found"
                    HTTP_UNAUTHORIZED, HTTP_FORBIDDEN -> Unavailable.ACCOUNT

                    HTTP_TOO_MANY -> Unavailable.OVER_QUOTA

                    else -> Unavailable.ERROR
                }
            // The message only: GraphHopper's error bodies never echo the key.
            val message =
                runCatching {
                    JSON
                        .parseToJsonElement(result.body)
                        .jsonObject["message"]
                        ?.jsonPrimitive
                        ?.contentOrNull
                }.getOrNull()
            return ProviderException(reason, "GraphHopper ${result.code}: ${message.orEmpty()}")
        }

        fun parse(body: String): Route {
            val path =
                JSON
                    .parseToJsonElement(body)
                    .jsonObject["paths"]!!
                    .jsonArray
                    .first()
                    .jsonObject
            val multiplier = path["points_encoded_multiplier"]?.jsonPrimitive?.doubleOrNull ?: DEFAULT_MULTIPLIER
            val precision = Math.round(Math.log10(multiplier)).toInt()
            val steps =
                path["instructions"]!!.jsonArray.mapIndexed {
                    i,
                    instruction,
                    ->
                    step(i, instruction.jsonObject)
                }
            return Route(
                backend = RoutingBackend.OSM_GRAPHHOPPER,
                points = Polyline.decode(path["points"]!!.jsonPrimitive.content, precision),
                steps = steps,
                distanceM = path["distance"]!!.jsonPrimitive.double,
                durationS = path["time"]!!.jsonPrimitive.double.toLong() / MS_PER_S,
                mode = TravelMode.DRIVING,
                attribution = ATTRIBUTION,
                via = Routes.mainRoad(steps),
            )
        }

        private fun step(
            index: Int,
            i: JsonObject,
        ): RouteStep =
            RouteStep(
                maneuver = if (index == 0) StepManeuver.DEPART else GraphHopperSigns.of(i["sign"]!!.jsonPrimitive.int),
                pointIndex =
                    i["interval"]!!
                        .jsonArray
                        .first()
                        .jsonPrimitive.int,
                roadName = i["street_name"]?.jsonPrimitive?.contentOrNull.orEmpty(),
                distanceM = i["distance"]!!.jsonPrimitive.double,
                roundaboutExit = i["exit_number"]?.jsonPrimitive?.intOrNull ?: 0,
            )
    }
}

/** GraphHopper's instruction signs (its API docs) as the protocol's maneuvers. */
internal object GraphHopperSigns {
    @Suppress("MagicNumber", "CyclomaticComplexMethod") // GraphHopper's numbering, one line per sign
    fun of(sign: Int): StepManeuver =
        when (sign) {
            -98, -8, 8 -> StepManeuver.UTURN
            -7 -> StepManeuver.KEEP_LEFT
            -3 -> StepManeuver.SHARP_LEFT
            -2 -> StepManeuver.LEFT
            -1 -> StepManeuver.SLIGHT_LEFT
            1 -> StepManeuver.SLIGHT_RIGHT
            2 -> StepManeuver.RIGHT
            3 -> StepManeuver.SHARP_RIGHT
            4 -> StepManeuver.ARRIVE
            6 -> StepManeuver.ROUNDABOUT
            7 -> StepManeuver.KEEP_RIGHT
            else -> StepManeuver.STRAIGHT // 0 continue, 5 a stop reached, unknown signs
        }
}
