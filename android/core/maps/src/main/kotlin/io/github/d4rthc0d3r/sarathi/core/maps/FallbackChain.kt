package io.github.d4rthc0d3r.sarathi.core.maps

import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.withTimeout

/** One backend skipped on the way, and why ("Google over quota"). */
data class Skipped<B>(
    val backend: B,
    val reason: Unavailable,
    /** The error's own message, for logs. */
    val detail: String? = null,
)

/** What the chain gave: the result and its backend, or nothing; every skip in order either way. */
data class ChainResult<B, T>(
    val backend: B?,
    val value: T?,
    val skipped: List<Skipped<B>>,
)

/**
 * Tries each backend in order (ADR-0006 "Fallback rules"): skips ones that can't be used (no key,
 * over quota) without calling them, and moves past a failure, a timeout or "no route". Never
 * calls a backend whose terms or key aren't set up.
 */
object FallbackChain {
    const val SEARCH_TIMEOUT_MS = 10_000L
    const val ROUTE_TIMEOUT_MS = 15_000L

    suspend fun <B, P, T> run(
        chain: List<B>,
        provider: (B) -> P?,
        unavailable: (P) -> Unavailable?,
        timeoutMs: Long,
        attempt: suspend (P) -> T,
    ): ChainResult<B, T> {
        val skipped = mutableListOf<Skipped<B>>()
        for (backend in chain) {
            val p = provider(backend)
            val blocked = if (p == null) Unavailable.NO_KEY else unavailable(p)
            if (blocked != null || p == null) {
                skipped += Skipped(backend, blocked ?: Unavailable.NO_KEY)
                continue
            }
            try {
                return ChainResult(backend, withTimeout(timeoutMs) { attempt(p) }, skipped)
            } catch (e: TimeoutCancellationException) {
                skipped += Skipped(backend, Unavailable.TIMEOUT, e.message)
            } catch (e: ProviderException) {
                skipped += Skipped(backend, e.reason, e.message)
            }
        }
        return ChainResult(null, null, skipped)
    }
}
