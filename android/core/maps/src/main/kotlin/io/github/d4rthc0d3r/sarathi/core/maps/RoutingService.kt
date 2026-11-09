package io.github.d4rthc0d3r.sarathi.core.maps

/**
 * Routes through the fallback chain (ADR-0006), with the cost guard around the backends that
 * bill: Google per destination in the request (every stop, waypoints included), Mappls per call.
 */
class RoutingService(
    private val provider: (RoutingBackend) -> RoutingProvider?,
    private val settings: () -> MapsSettings,
    private val guard: () -> CostGuard,
) {
    suspend fun route(request: RouteRequest): ChainResult<RoutingBackend, Route> =
        FallbackChain.run(
            settings().routingChain(),
            provider,
            { p -> p.unavailable() ?: overQuota(p.backend, request) },
            FallbackChain.ROUTE_TIMEOUT_MS,
        ) { p ->
            val meter = meterOf(p.backend)
            if (meter != null) guard().record(meter, billed(p.backend, request))
            try {
                p.route(request)
            } catch (e: ProviderException) {
                if (e.reason == Unavailable.OVER_QUOTA && meter != null) guard().blockForPeriod(meter)
                throw e
            }
        }

    private fun overQuota(
        backend: RoutingBackend,
        request: RouteRequest,
    ): Unavailable? {
        val meter = meterOf(backend) ?: return null
        val s = guard().status(meter)
        // Blocked, or this request would go past the block threshold's cap.
        return if (s.level == GuardLevel.BLOCKED ||
            s.used + billed(backend, request) > s.cap
        ) {
            Unavailable.OVER_QUOTA
        } else {
            null
        }
    }

    companion object {
        fun meterOf(backend: RoutingBackend): Meter? =
            when (backend) {
                RoutingBackend.GOOGLE -> Meter.GOOGLE_NAV_DESTINATIONS
                RoutingBackend.MAPPLS -> Meter.MAPPLS_ROUTING
                else -> null
            }

        /** Google bills one destination per stop; everything else counts one call. */
        fun billed(
            backend: RoutingBackend,
            request: RouteRequest,
        ): Int = if (backend == RoutingBackend.GOOGLE) request.stops.size else 1
    }
}
