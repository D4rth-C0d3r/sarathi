package io.github.d4rthc0d3r.sarathi.core.osmcontrib

/** What the rider can send to OSM (ADR-0012 §4). */
enum class Contribution { TRACE, NOTE, EDIT }

/**
 * Our own limits on what goes to OSM, on top of OSM's (ADR-0012 §5): at most [PER_DAY] of each
 * kind per UTC day. Kept on the phone as `day;kind;count` lines.
 */
class DailyLimits(
    private val counts: MutableMap<Pair<Long, Contribution>, Int> = mutableMapOf(),
) {
    fun allowed(
        kind: Contribution,
        nowUtcMs: Long,
    ): Boolean = used(kind, nowUtcMs) < PER_DAY

    fun used(
        kind: Contribution,
        nowUtcMs: Long,
    ): Int = counts[day(nowUtcMs) to kind] ?: 0

    fun record(
        kind: Contribution,
        nowUtcMs: Long,
    ) {
        val key = day(nowUtcMs) to kind
        counts[key] = (counts[key] ?: 0) + 1
        counts.keys.removeAll { it.first < key.first } // only today matters
    }

    fun lines(): List<String> = counts.map { (k, n) -> "${k.first};${k.second.name};$n" }

    companion object {
        const val PER_DAY = 5
        private const val MS_PER_DAY = 24L * 60 * 60 * 1000
        private const val FIELDS = 3

        private fun day(utcMs: Long) = utcMs / MS_PER_DAY

        fun read(lines: Sequence<String>): DailyLimits {
            val counts = mutableMapOf<Pair<Long, Contribution>, Int>()
            for (line in lines) {
                val f = line.split(';').takeIf { it.size == FIELDS } ?: continue
                val day = f[0].toLongOrNull()
                val kind = Contribution.entries.firstOrNull { it.name == f[1] }
                val n = f[2].toIntOrNull()
                if (day != null && kind != null && n != null) counts[day to kind] = n
            }
            return DailyLimits(counts)
        }
    }
}
