package io.github.d4rthc0d3r.sarathi.core.maps

import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import kotlinx.serialization.Serializable

/**
 * A routed ride kept on the phone for desk tests (step F8): replayed as a simulated ride with
 * no backend calls, and scripted traffic. Google routes aren't kept: Google's terms limit
 * caching its content (ADR-0006). Never leaves the phone; never in the repository.
 */
@Serializable
data class DeskRecording(
    val destination: String,
    val route: Route,
    val request: RouteRequest,
    val recordedAtMs: Long,
) {
    companion object {
        fun canRecord(route: Route) = route.backend != RoutingBackend.GOOGLE
    }
}

/**
 * Traffic for a replayed ride: the recording's own, then every [STEP_MS] (the protocol's
 * shortest gap between traffic messages) a change the dash should get: a jam at 35-40 % of the
 * route, a slow stretch at 55-65 %, then the jam cleared.
 */
object TrafficScript {
    const val STEP_MS = 30_000L
    private const val JAM_FROM = 0.35
    private const val JAM_TO = 0.40
    private const val SLOW_FROM = 0.55
    private const val SLOW_TO = 0.65

    fun updates(
        route: Route,
        stepMs: Long = STEP_MS,
    ): Flow<TrafficUpdate> =
        flow {
            val l = route.distanceM
            val base = route.traffic?.stretches?.ifEmpty { null } ?: listOf(TrafficStretch(0.0, l, TrafficLevel.FREE))
            val eta = route.traffic?.etaS ?: route.durationS
            val jammed = overlay(base, l * JAM_FROM, l * JAM_TO, TrafficLevel.JAM)
            val slowed = overlay(jammed, l * SLOW_FROM, l * SLOW_TO, TrafficLevel.SLOW)
            val cleared = overlay(slowed, l * JAM_FROM, l * JAM_TO, TrafficLevel.FREE)
            for (stretches in listOf(jammed, slowed, cleared)) {
                delay(stepMs)
                emit(TrafficUpdate(stretches, eta))
            }
        }

    /** [base] with [from]..[to] set to [level], neighbours of the same level merged. */
    fun overlay(
        base: List<TrafficStretch>,
        from: Double,
        to: Double,
        level: TrafficLevel,
    ): List<TrafficStretch> {
        val pieces = mutableListOf(TrafficStretch(from, to - from, level))
        for (s in base) {
            val end = s.offsetM + s.lengthM
            if (s.offsetM < from) pieces += s.copy(lengthM = minOf(end, from) - s.offsetM)
            if (end > to) pieces += TrafficStretch(maxOf(s.offsetM, to), end - maxOf(s.offsetM, to), s.level)
        }
        return pieces
            .filter { it.lengthM > 0 }
            .sortedBy { it.offsetM }
            .fold(mutableListOf()) { acc, p ->
                val last = acc.lastOrNull()
                if (last != null &&
                    last.level == p.level
                ) {
                    acc[acc.lastIndex] = last.copy(lengthM = last.lengthM + p.lengthM)
                } else {
                    acc +=
                        p
                }
                acc
            }
    }
}
