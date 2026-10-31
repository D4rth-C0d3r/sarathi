package io.github.d4rthc0d3r.sarathi.core.maps

import kotlinx.serialization.Serializable

/**
 * The rider's maps settings (ADR-0006): one backend for search and routing, optional advanced
 * overrides, the fallback order (OSM offline always last), Mappls's daily caps and the Valhalla
 * server (a setting, per FOSSGIS's terms).
 */
@Serializable
data class MapsSettings(
    val backend: SearchBackend = SearchBackend.GOOGLE,
    val searchOverride: SearchBackend? = null,
    val routingOverride: RoutingBackend? = null,
    /** Routing fallback order, without OSM_OFFLINE (pinned last). */
    val fallback: List<RoutingBackend> = DEFAULT_FALLBACK,
    val mapplsSearchPerDay: Int = 200,
    val mapplsRoutesPerDay: Int = 100,
    val valhallaUrl: String = DEFAULT_VALHALLA,
) {
    val search: SearchBackend get() = searchOverride ?: backend

    val routing: RoutingBackend
        get() =
            routingOverride ?: when (backend) {
                SearchBackend.GOOGLE -> RoutingBackend.GOOGLE
                SearchBackend.MAPPLS -> RoutingBackend.MAPPLS
                SearchBackend.OSM -> RoutingBackend.OSM_VALHALLA
            }

    /** The routing backends to try, in order: the chosen one, the fallback order, then offline. */
    fun routingChain(): List<RoutingBackend> =
        (listOf(routing) + fallback + RoutingBackend.OSM_OFFLINE).distinct().let { chain ->
            chain.filter { it != RoutingBackend.OSM_OFFLINE } + RoutingBackend.OSM_OFFLINE
        }

    /** The search backends to try, in order: the chosen one, then Google, Mappls, Photon. */
    fun searchChain(): List<SearchBackend> = (listOf(search) + SearchBackend.entries).distinct()

    /** Moves [backend] one place earlier (-1) or later (+1) in the fallback order. */
    fun move(
        backend: RoutingBackend,
        by: Int,
    ): MapsSettings {
        val i = fallback.indexOf(backend)
        val j = i + by
        if (i < 0 || j !in fallback.indices) return this
        return copy(fallback = fallback.toMutableList().apply { add(j, removeAt(i)) })
    }

    companion object {
        const val DEFAULT_VALHALLA = "https://valhalla1.openstreetmap.de"
        val DEFAULT_FALLBACK =
            listOf(
                RoutingBackend.GOOGLE,
                RoutingBackend.MAPPLS,
                RoutingBackend.OSM_VALHALLA,
                RoutingBackend.OSM_GRAPHHOPPER,
            )
    }
}
