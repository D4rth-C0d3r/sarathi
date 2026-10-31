package io.github.d4rthc0d3r.sarathi.core.ble

/**
 * How long to wait before trying to connect again after a failure the Bluetooth stack doesn't
 * retry by itself (for example status 133). Doubles from [firstMs] up to [maxMs]; a connection
 * that reaches the dash's hello resets it. Never zero, so a broken link never spins.
 */
class ReconnectPolicy(
    private val firstMs: Long = 1_000,
    private val maxMs: Long = 30_000,
) {
    private var nextMs = firstMs

    fun nextDelayMs(): Long {
        val delay = nextMs
        nextMs = (nextMs * 2).coerceAtMost(maxMs)
        return delay
    }

    fun connected() {
        nextMs = firstMs
    }
}
