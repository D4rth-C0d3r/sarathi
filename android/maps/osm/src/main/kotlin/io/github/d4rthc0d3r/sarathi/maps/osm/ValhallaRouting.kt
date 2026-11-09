package io.github.d4rthc0d3r.sarathi.maps.osm

import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.maps.Polyline
import io.github.d4rthc0d3r.sarathi.core.maps.ProviderException
import io.github.d4rthc0d3r.sarathi.core.maps.Route
import io.github.d4rthc0d3r.sarathi.core.maps.RouteRequest
import io.github.d4rthc0d3r.sarathi.core.maps.RouteStep
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingProvider
import io.github.d4rthc0d3r.sarathi.core.maps.StepManeuver
import io.github.d4rthc0d3r.sarathi.core.maps.TravelMode
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable
import kotlinx.coroutines.delay
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.add
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.double
import kotlinx.serialization.json.int
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.put
import kotlinx.serialization.json.putJsonArray
import kotlinx.serialization.json.putJsonObject
import okhttp3.HttpUrl.Companion.toHttpUrl
import okhttp3.OkHttpClient

/**
 * OSM online routing with Valhalla (ADR-0006), by default FOSSGIS's public server: at most one
 * request a second and one at a time, an identifying User-Agent, the URL a setting. Valhalla's
 * `motorcycle` costing is the two-wheeler profile.
 */
class ValhallaRouting(
    private val http: OkHttpClient,
    private val userAgent: String,
    private val baseUrl: () -> String,
    private val clock: () -> Long = System::currentTimeMillis,
) : RoutingProvider {
    override val backend = RoutingBackend.OSM_VALHALLA

    private val gate = Mutex()
    private var lastRequestMs = 0L

    override fun unavailable(): Unavailable? = null

    override suspend fun route(request: RouteRequest): Route {
        val url = "${baseUrl().trimEnd('/')}/route".toHttpUrl()
        val result =
            gate.withLock {
                delay((lastRequestMs + MIN_INTERVAL_MS - clock()).coerceAtLeast(0))
                lastRequestMs = clock()
                http.postJson(url, userAgent, body(request).toString())
            }
        return when {
            result.ok -> parse(result.body, request.mode)
            result.code == HTTP_TOO_MANY -> throw ProviderException(Unavailable.OVER_QUOTA, "Valhalla ${result.code}")
            else -> throw failure(result)
        }
    }

    companion object {
        /** FOSSGIS's terms: at most one request a second. */
        const val MIN_INTERVAL_MS = 1_000L
        const val ATTRIBUTION = "© OpenStreetMap contributors · Valhalla"
        private const val PRECISION = 6
        private const val M_PER_KM = 1000.0
        private val JSON = Json { ignoreUnknownKeys = true }

        /** Valhalla's "no route" errors: 442 no path, 171 no road near a location (and 1xx kin). */
        private val NO_ROUTE_CODES = setOf(171, 442, 443)

        fun body(request: RouteRequest): JsonObject =
            buildJsonObject {
                putJsonArray("locations") {
                    (listOf(request.origin) + request.stops).forEach {
                        add(
                            buildJsonObject {
                                put("lat", it.lat)
                                put("lon", it.lng)
                            },
                        )
                    }
                }
                put("costing", if (request.mode == TravelMode.TWO_WHEELER) "motorcycle" else "auto")
                putJsonObject("directions_options") {
                    put("units", "kilometers")
                    put("language", "en-US")
                }
            }

        private fun failure(result: HttpResult): ProviderException {
            val code =
                runCatching {
                    JSON
                        .parseToJsonElement(result.body)
                        .jsonObject["error_code"]
                        ?.jsonPrimitive
                        ?.intOrNull
                }.getOrNull()
            val reason = if (code in NO_ROUTE_CODES) Unavailable.NO_ROUTE else Unavailable.ERROR
            return ProviderException(reason, "Valhalla HTTP ${result.code}, error $code")
        }

        /** Valhalla's trip: legs joined into one line, maneuvers into steps. */
        fun parse(
            body: String,
            mode: TravelMode,
        ): Route {
            val trip = JSON.parseToJsonElement(body).jsonObject["trip"]!!.jsonObject
            val points = mutableListOf<LatLng>()
            val steps = mutableListOf<RouteStep>()
            trip["legs"]!!.jsonArray.forEachIndexed { legIndex, leg ->
                val shape = Polyline.decode(leg.jsonObject["shape"]!!.jsonPrimitive.content, PRECISION)
                // Legs share their joining point; the next leg's first point is dropped.
                val offset = if (legIndex == 0) 0 else points.size - 1
                points += if (legIndex == 0) shape else shape.drop(1)
                leg.jsonObject["maneuvers"]!!.jsonArray.forEach { m -> addStep(steps, m.jsonObject, offset, legIndex) }
            }
            val summary = trip["summary"]!!.jsonObject
            return Route(
                backend = RoutingBackend.OSM_VALHALLA,
                points = points,
                steps = steps,
                distanceM = summary["length"]!!.jsonPrimitive.double * M_PER_KM,
                durationS = summary["time"]!!.jsonPrimitive.double.toLong(),
                mode = mode,
                attribution = ATTRIBUTION,
                via = Routes.mainRoad(steps),
            )
        }

        private fun addStep(
            steps: MutableList<RouteStep>,
            m: JsonObject,
            offset: Int,
            legIndex: Int,
        ) {
            val type = m["type"]!!.jsonPrimitive.int
            val length = m["length"]!!.jsonPrimitive.double * M_PER_KM
            val maneuver = ValhallaManeuvers.of(type, firstLeg = legIndex == 0)
            if (maneuver == null) {
                // Leaving a roundabout, or a middle stop's departure: part of the step before.
                steps.lastOrNull()?.let { steps[steps.lastIndex] = it.copy(distanceM = it.distanceM + length) }
                return
            }
            val names = (m["street_names"] as? JsonArray)?.mapNotNull { it.jsonPrimitive.contentOrNull }.orEmpty()
            steps +=
                RouteStep(
                    maneuver = maneuver,
                    pointIndex = offset + m["begin_shape_index"]!!.jsonPrimitive.int,
                    roadName = names.firstOrNull().orEmpty(),
                    distanceM = length,
                    roundaboutExit = m["roundabout_exit_count"]?.jsonPrimitive?.intOrNull ?: 0,
                )
        }
    }
}

/** Valhalla's maneuver types (its API docs) as the protocol's maneuvers. */
internal object ValhallaManeuvers {
    @Suppress("MagicNumber", "CyclomaticComplexMethod") // Valhalla's numbering, one line per type
    fun of(
        type: Int,
        firstLeg: Boolean,
    ): StepManeuver? =
        when (type) {
            1, 2, 3 -> if (firstLeg) StepManeuver.DEPART else null
            4, 5, 6 -> StepManeuver.ARRIVE
            9 -> StepManeuver.SLIGHT_RIGHT
            10 -> StepManeuver.RIGHT
            11 -> StepManeuver.SHARP_RIGHT
            12, 13 -> StepManeuver.UTURN
            14 -> StepManeuver.SHARP_LEFT
            15 -> StepManeuver.LEFT
            16 -> StepManeuver.SLIGHT_LEFT
            18, 20 -> StepManeuver.RAMP_RIGHT
            19, 21 -> StepManeuver.RAMP_LEFT
            23 -> StepManeuver.KEEP_RIGHT
            24 -> StepManeuver.KEEP_LEFT
            25, 37, 38 -> StepManeuver.MERGE
            26 -> StepManeuver.ROUNDABOUT
            27 -> null
            28 -> StepManeuver.FERRY
            else -> StepManeuver.STRAIGHT
        }
}

internal object Routes {
    /** The longest named stretch, for "via NH 66" on the confirmation screen. */
    fun mainRoad(steps: List<RouteStep>): String =
        steps
            .filter { it.roadName.isNotBlank() }
            .groupBy { it.roadName }
            .maxByOrNull { (_, s) -> s.sumOf { it.distanceM } }
            ?.key
            .orEmpty()
}
