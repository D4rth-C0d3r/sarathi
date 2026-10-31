package io.github.d4rthc0d3r.sarathi.core.maps

import kotlinx.serialization.Serializable
import java.time.Instant
import java.time.ZoneId

/** What the cost guard counts (ADR-0006). */
@Serializable
enum class Meter(
    val period: Period,
) {
    GOOGLE_NAV_DESTINATIONS(Period.GOOGLE_MONTH),
    GOOGLE_AUTOCOMPLETE(Period.GOOGLE_MONTH),
    GOOGLE_PLACE_DETAILS(Period.GOOGLE_MONTH),
    GOOGLE_GEOCODING(Period.GOOGLE_MONTH),
    MAPPLS_SEARCH(Period.LOCAL_DAY),
    MAPPLS_ROUTING(Period.LOCAL_DAY),
}

/** Google bills by calendar month in Pacific time; Mappls's caps are per local day. */
enum class Period { GOOGLE_MONTH, LOCAL_DAY }

enum class GuardLevel { OK, WARN, BLOCKED }

data class MeterStatus(
    val meter: Meter,
    val used: Int,
    val cap: Int,
    val level: GuardLevel,
)

/** Counts per meter, for the period they belong to; kept by the app between runs. */
@Serializable
data class Usage(
    val counts: Map<Meter, Count> = emptyMap(),
) {
    @Serializable
    data class Count(
        val period: String,
        val used: Int = 0,
        /** The backend said "over quota": blocked for the rest of this period. */
        val blocked: Boolean = false,
    )
}

/**
 * The cost guard (ADR-0006): counts usage locally per billing period, warns at 80 % and blocks at
 * 95 % of the free cap, and blocks a backend for the rest of the period when it reports a quota
 * error. Caps start at Google's global free tier (1,000 destinations; 10,000 per Places SKU).
 */
class CostGuard(
    private val caps: Map<Meter, Int>,
    private val clock: () -> Instant = Instant::now,
    private val localZone: ZoneId = ZoneId.systemDefault(),
    initial: Usage = Usage(),
    private val save: (Usage) -> Unit = {},
) {
    var usage: Usage = initial
        private set

    fun status(meter: Meter): MeterStatus {
        val count = current(meter)
        val cap = caps[meter] ?: Int.MAX_VALUE
        val level =
            when {
                count.blocked || count.used >= cap * BLOCK_AT -> GuardLevel.BLOCKED
                count.used >= cap * WARN_AT -> GuardLevel.WARN
                else -> GuardLevel.OK
            }
        return MeterStatus(meter, count.used, cap, level)
    }

    fun blocked(meter: Meter) = status(meter).level == GuardLevel.BLOCKED

    /** Counts [n] uses (for example one destination per route stop). */
    fun record(
        meter: Meter,
        n: Int = 1,
    ) = put(meter, current(meter).let { it.copy(used = it.used + n) })

    /** The backend reported a quota error: blocked until the period ends. */
    fun blockForPeriod(meter: Meter) = put(meter, current(meter).copy(blocked = true))

    private fun current(meter: Meter): Usage.Count {
        val period = periodKey(meter.period)
        val count = usage.counts[meter]
        return if (count?.period == period) count else Usage.Count(period)
    }

    private fun put(
        meter: Meter,
        count: Usage.Count,
    ) {
        usage = usage.copy(counts = usage.counts + (meter to count))
        save(usage)
    }

    private fun periodKey(period: Period): String {
        val now = clock()
        return when (period) {
            Period.GOOGLE_MONTH -> now.atZone(PACIFIC).let { "%04d-%02d".format(it.year, it.monthValue) }
            Period.LOCAL_DAY -> now.atZone(localZone).toLocalDate().toString()
        }
    }

    companion object {
        const val WARN_AT = 0.80
        const val BLOCK_AT = 0.95

        // Google's global free tier per month (ADR-0006), the stricter list.
        const val GOOGLE_DESTINATIONS_FREE = 1_000
        const val GOOGLE_PLACES_FREE = 10_000
        private val PACIFIC: ZoneId = ZoneId.of("America/Los_Angeles")

        /** Google's global free caps per month (ADR-0006); Mappls's daily caps come from settings. */
        fun caps(settings: MapsSettings): Map<Meter, Int> =
            mapOf(
                Meter.GOOGLE_NAV_DESTINATIONS to GOOGLE_DESTINATIONS_FREE,
                Meter.GOOGLE_AUTOCOMPLETE to GOOGLE_PLACES_FREE,
                Meter.GOOGLE_PLACE_DETAILS to GOOGLE_PLACES_FREE,
                Meter.GOOGLE_GEOCODING to GOOGLE_PLACES_FREE,
                Meter.MAPPLS_SEARCH to settings.mapplsSearchPerDay,
                Meter.MAPPLS_ROUTING to settings.mapplsRoutesPerDay,
            )
    }
}
