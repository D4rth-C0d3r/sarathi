package io.github.d4rthc0d3r.sarathi.core.maps

/**
 * A desk test of the fallback chain (step F8, ADR-0006): while on, each routing request fails
 * one more stage, before any call is made (no cost): Google over quota, then Mappls with an
 * error, then OSM online offline, which leaves OSM offline. Debug builds only; off after the
 * app restarts.
 */
class FailureDrill {
    @Volatile var active = false
        private set

    @Volatile private var requests = 0

    fun start() {
        requests = 0
        active = true
    }

    fun stop() {
        active = false
    }

    /** True while a reroute would fail one more backend: the simulated ride should leave the road. */
    val reroutesDue: Boolean get() = active && requests < STAGES.size

    /** The failures for the next routing request. */
    fun nextFailures(): Map<RoutingBackend, Unavailable> {
        if (!active) return emptyMap()
        val stage = requests++
        return STAGES.take((stage + 1).coerceAtMost(STAGES.size)).fold(emptyMap()) { acc, m -> acc + m }
    }

    companion object {
        /** The chain while drilling: the order the drill walks, whatever the settings say. */
        val CHAIN =
            listOf(
                RoutingBackend.GOOGLE,
                RoutingBackend.MAPPLS,
                RoutingBackend.OSM_VALHALLA,
                RoutingBackend.OSM_GRAPHHOPPER,
                RoutingBackend.OSM_OFFLINE,
            )

        val STAGES =
            listOf(
                mapOf(RoutingBackend.GOOGLE to Unavailable.OVER_QUOTA),
                mapOf(RoutingBackend.MAPPLS to Unavailable.ERROR),
                mapOf(
                    RoutingBackend.OSM_VALHALLA to Unavailable.OFFLINE,
                    RoutingBackend.OSM_GRAPHHOPPER to Unavailable.OFFLINE,
                ),
            )
    }
}
