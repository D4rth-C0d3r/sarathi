package io.github.d4rthc0d3r.sarathi.core.maps

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.emptyFlow

/** Why a backend wasn't used, in words the app shows ("Google over quota → Mappls"). */
enum class Unavailable {
    NO_KEY, // key or credentials not built in
    NO_TERMS, // the rider hasn't accepted the backend's terms
    OVER_QUOTA, // the cost guard blocks it, or the backend said so
    ACCOUNT, // billing disabled, trial ended
    OFFLINE, // no network
    NO_DATA, // offline tiles missing
    NO_ROUTE,
    TIMEOUT,
    ERROR,
}

/** Thrown by a provider for a failure the fallback chain should move past. */
class ProviderException(
    val reason: Unavailable,
    message: String = reason.name,
    cause: Throwable? = null,
) : Exception(message, cause)

interface SearchProvider {
    val backend: SearchBackend

    /** Null if usable now, else why not (no key, over quota, ...). */
    fun unavailable(): Unavailable?

    suspend fun autocomplete(
        query: String,
        near: LatLng?,
        session: SearchSession,
    ): List<Suggestion>

    suspend fun details(
        suggestion: Suggestion,
        session: SearchSession,
    ): Place

    suspend fun reverseGeocode(at: LatLng): Place? = null
}

interface RoutingProvider {
    val backend: RoutingBackend
    val hasTraffic: Boolean get() = false

    fun unavailable(): Unavailable?

    suspend fun route(request: RouteRequest): Route

    /** After leaving the route; by default a fresh route from [from] to the remaining stops. */
    suspend fun reroute(
        from: LatLng,
        remaining: RouteRequest,
    ): Route = route(remaining.copy(origin = from))

    /** Traffic for [route], refreshed while collected; empty without traffic. */
    fun traffic(route: Route): Flow<TrafficUpdate> = emptyFlow()
}
