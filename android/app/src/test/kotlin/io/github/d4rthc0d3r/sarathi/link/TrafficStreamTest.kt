package io.github.d4rthc0d3r.sarathi.link

import io.github.d4rthc0d3r.sarathi.core.maps.TrafficLevel
import io.github.d4rthc0d3r.sarathi.core.maps.TrafficStretch
import io.github.d4rthc0d3r.sarathi.core.maps.TrafficUpdate
import io.github.d4rthc0d3r.sarathi.core.protocol.BinaryCodec
import io.github.d4rthc0d3r.sarathi.core.protocol.Traffic
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class TrafficStreamTest {
    private var now = 0L
    private val stream = TrafficStream(routeId = 7, version = 1) { now }

    private fun update(vararg s: TrafficStretch) = TrafficUpdate(s.toList(), etaS = 600)

    @Test
    fun sendsOnChangeAtMostEvery30s() {
        val a = update(TrafficStretch(0.0, 1000.0, TrafficLevel.FREE), TrafficStretch(1000.0, 200.0, TrafficLevel.JAM))
        val b = update(TrafficStretch(0.0, 1200.0, TrafficLevel.FREE))
        assertNotNull(stream.offer(a))
        assertNull(stream.offer(a)) // unchanged
        now = 10_000
        assertNull(stream.offer(b)) // changed, but too soon
        now = 30_000
        val msg = stream.offer(b)!!
        assertEquals(1, msg.entries.size)
        assertEquals(7L, msg.routeId)
    }

    @Test
    fun roundsWithoutOverlapAndEncodes() {
        val entries =
            TrafficStream.entries(
                listOf(
                    TrafficStretch(0.0, 100.4, TrafficLevel.FREE),
                    TrafficStretch(100.4, 0.3, TrafficLevel.SLOW), // under a metre: dropped
                    TrafficStretch(100.7, 49.6, TrafficLevel.JAM),
                ),
            )
        assertEquals(listOf(0L to 100L, 101L to 49L), entries.map { it.offsetM to it.lengthM })
        // Valid for the protocol: encodes and decodes back.
        val msg = Traffic(1, 0, entries)
        assertEquals(msg, BinaryCodec.decode(msg.type, BinaryCodec.encode(msg)))
    }

    @Test
    fun keepsTheNearestEntriesWhenThereAreTooMany() {
        val many =
            (0 until 300).map {
                TrafficStretch(
                    it * 10.0,
                    10.0,
                    if (it % 2 ==
                        0
                    ) {
                        TrafficLevel.FREE
                    } else {
                        TrafficLevel.SLOW
                    },
                )
            }
        val entries = TrafficStream.entries(many)
        assertEquals(Traffic.MAX_ENTRIES, entries.size)
        assertEquals(0L, entries.first().offsetM)
    }
}
