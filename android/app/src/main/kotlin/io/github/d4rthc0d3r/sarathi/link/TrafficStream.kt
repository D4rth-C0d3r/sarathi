package io.github.d4rthc0d3r.sarathi.link

import io.github.d4rthc0d3r.sarathi.core.maps.TrafficStretch
import io.github.d4rthc0d3r.sarathi.core.maps.TrafficUpdate
import io.github.d4rthc0d3r.sarathi.core.protocol.Traffic
import io.github.d4rthc0d3r.sarathi.core.protocol.TrafficEntry
import io.github.d4rthc0d3r.sarathi.core.maps.TrafficLevel as MapsLevel
import io.github.d4rthc0d3r.sarathi.core.protocol.TrafficLevel as DashLevel

/**
 * Traffic for the dash (protocol section 4, `traffic`): the route's stretches as whole metres,
 * sent when they change and at most every [MIN_INTERVAL_MS] (section 7). One stream per route
 * version; a reroute starts a new one.
 */
class TrafficStream(
    private val routeId: Long,
    private val version: Int,
    private val clock: () -> Long = System::currentTimeMillis,
) {
    private var sent: List<TrafficEntry>? = null
    private var sentAtMs = 0L

    /** The message to send for [update] now, or null if it's unchanged or too soon. */
    fun offer(update: TrafficUpdate): Traffic? {
        val entries = entries(update.stretches)
        val now = clock()
        if (entries == sent || (sent != null && now - sentAtMs < MIN_INTERVAL_MS)) return null
        sent = entries
        sentAtMs = now
        return Traffic(routeId, version, entries)
    }

    companion object {
        const val MIN_INTERVAL_MS = 30_000L

        /**
         * Whole metres, sorted and not overlapping. Over the protocol's 256 entries, the ones
         * nearest the start are kept: the far end matters least and is refreshed before the rider
         * gets there.
         */
        fun entries(stretches: List<TrafficStretch>): List<TrafficEntry> {
            val out = mutableListOf<TrafficEntry>()
            // Under a metre isn't worth an entry; judged before rounding.
            for (s in stretches.filter { it.lengthM >= 1.0 }.sortedBy { it.offsetM }) {
                val start = maxOf(Math.round(s.offsetM), out.lastOrNull()?.let { it.offsetM + it.lengthM } ?: 0L)
                val length = Math.round(s.offsetM + s.lengthM) - start
                if (length >= 1) out += TrafficEntry(start, length, level(s.level))
                if (out.size == Traffic.MAX_ENTRIES) break
            }
            return out
        }

        private fun level(level: MapsLevel): DashLevel =
            when (level) {
                MapsLevel.FREE -> DashLevel.FREE
                MapsLevel.SLOW -> DashLevel.SLOW
                MapsLevel.JAM -> DashLevel.JAM
                MapsLevel.UNKNOWN -> DashLevel.UNKNOWN
            }
    }
}
