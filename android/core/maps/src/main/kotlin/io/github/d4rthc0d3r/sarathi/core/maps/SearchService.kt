package io.github.d4rthc0d3r.sarathi.core.maps

import kotlinx.coroutines.CancellationException

/**
 * Search across backends (ADR-0006, ADR-0008): autocomplete through the fallback chain, details
 * on the backend that made the suggestion, and the cost guard's counting around both.
 */
class SearchService(
    private val provider: (SearchBackend) -> SearchProvider?,
    private val settings: () -> MapsSettings,
    private val guard: () -> CostGuard,
) {
    suspend fun autocomplete(
        query: String,
        near: LatLng?,
        session: SearchSession,
    ): ChainResult<SearchBackend, List<Suggestion>> =
        FallbackChain.run(
            settings().searchChain(),
            provider,
            { p -> p.unavailable() ?: overQuota(autocompleteMeter(p.backend)) },
            FallbackChain.SEARCH_TIMEOUT_MS,
        ) { p -> metered(autocompleteMeter(p.backend)) { p.autocomplete(query, near, session) } }

    /**
     * The place behind [suggestion], from the backend that suggested it. A suggestion that
     * already has coordinates needs no call, except Google's: its details call is what closes
     * the free autocomplete session.
     */
    suspend fun details(
        suggestion: Suggestion,
        session: SearchSession,
    ): Place {
        val at = suggestion.at
        if (at != null && suggestion.backend != SearchBackend.GOOGLE) {
            return Place(suggestion.title, suggestion.subtitle, at, sourceOf(suggestion.backend), suggestion.id)
        }
        val p = provider(suggestion.backend) ?: throw ProviderException(Unavailable.NO_KEY)
        val meter = detailsMeter(p.backend)
        (p.unavailable() ?: overQuota(meter))?.let { throw ProviderException(it) }
        return guarded { metered(meter) { p.details(suggestion, session) } }
    }

    /**
     * A name for a point, from the first backend that has one. Google's provider has no reverse
     * geocoding (no call, no cost), so it's passed over.
     */
    suspend fun reverseGeocode(at: LatLng): ChainResult<SearchBackend, Place> =
        FallbackChain
            .run(
                settings().searchChain(),
                provider,
                { p ->
                    p.unavailable()
                        ?: overQuota(detailsMeter(p.backend).takeIf { p.backend != SearchBackend.GOOGLE })
                },
                FallbackChain.SEARCH_TIMEOUT_MS,
            ) { p ->
                val meter = if (p.backend == SearchBackend.MAPPLS) Meter.MAPPLS_SEARCH else null
                metered(meter) { p.reverseGeocode(at) } ?: throw ProviderException(Unavailable.NO_DATA)
            }

    /** The top suggestion for [query] and its details: for links that carry only a name (ADR-0008). */
    suspend fun findByName(
        query: String,
        near: LatLng?,
        session: SearchSession,
    ): Place? {
        val top = autocomplete(query, near, session).value?.firstOrNull() ?: return null
        return details(top, session)
    }

    private fun overQuota(meter: Meter?): Unavailable? =
        if (meter != null && guard().blocked(meter)) Unavailable.OVER_QUOTA else null

    /** Counts a request before it's sent (a request that reaches the backend is billed). */
    private suspend fun <T> metered(
        meter: Meter?,
        call: suspend () -> T,
    ): T {
        if (meter == null) return call()
        guard().record(meter)
        try {
            return call()
        } catch (e: ProviderException) {
            if (e.reason == Unavailable.OVER_QUOTA) guard().blockForPeriod(meter)
            throw e
        }
    }

    /** Any other failure of a provider's SDK as an error the callers understand (ADR-0006). */
    private suspend fun <T> guarded(call: suspend () -> T): T =
        try {
            call()
        } catch (e: ProviderException) {
            throw e
        } catch (e: CancellationException) {
            throw e
        } catch (
            @Suppress("TooGenericExceptionCaught") e: Exception,
        ) {
            throw ProviderException(Unavailable.ERROR, e.toString(), e)
        }

    companion object {
        /**
         * Google's autocomplete is counted per request, though requests in a session that ends in
         * details are free: an overcount, on the safe side of the free tier.
         */
        fun autocompleteMeter(backend: SearchBackend): Meter? =
            when (backend) {
                SearchBackend.GOOGLE -> Meter.GOOGLE_AUTOCOMPLETE
                SearchBackend.MAPPLS -> Meter.MAPPLS_SEARCH
                SearchBackend.OSM -> null
            }

        fun detailsMeter(backend: SearchBackend): Meter? =
            when (backend) {
                SearchBackend.GOOGLE -> Meter.GOOGLE_PLACE_DETAILS
                SearchBackend.MAPPLS -> Meter.MAPPLS_SEARCH
                SearchBackend.OSM -> null
            }

        fun sourceOf(backend: SearchBackend): PlaceSource =
            when (backend) {
                SearchBackend.GOOGLE -> PlaceSource.GOOGLE
                SearchBackend.MAPPLS -> PlaceSource.MAPPLS
                SearchBackend.OSM -> PlaceSource.OSM
            }
    }
}
