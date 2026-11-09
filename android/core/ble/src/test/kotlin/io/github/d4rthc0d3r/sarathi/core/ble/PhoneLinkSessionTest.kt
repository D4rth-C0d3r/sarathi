package io.github.d4rthc0d3r.sarathi.core.ble

import io.github.d4rthc0d3r.sarathi.core.protocol.CallAction
import io.github.d4rthc0d3r.sarathi.core.protocol.CallChoice
import io.github.d4rthc0d3r.sarathi.core.protocol.Chunk
import io.github.d4rthc0d3r.sarathi.core.protocol.ChunkHave
import io.github.d4rthc0d3r.sarathi.core.protocol.ChunkKey
import io.github.d4rthc0d3r.sarathi.core.protocol.HelloPhone
import io.github.d4rthc0d3r.sarathi.core.protocol.PhoneBattery
import io.github.d4rthc0d3r.sarathi.core.protocol.PhoneCap
import io.github.d4rthc0d3r.sarathi.core.protocol.Point
import io.github.d4rthc0d3r.sarathi.core.protocol.Provenance
import io.github.d4rthc0d3r.sarathi.core.protocol.Route
import io.github.d4rthc0d3r.sarathi.core.protocol.Routing
import io.github.d4rthc0d3r.sarathi.core.protocol.Shape
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PhoneLinkSessionTest {
    private fun connect(dash: FakeDash = FakeDash()): Pair<FakeDash, PhoneLinkSession> {
        val session = PhoneLinkSession(dash, "0.1.0", setOf(PhoneCap.CALLS))
        dash.session = session
        return dash to session
    }

    // About 7 KB: 30 frames at MTU 247.
    private fun bigRoute() =
        Route(
            1,
            0,
            Routing.GOOGLE,
            1,
            Route.MODE_TWO_WHEELER,
            10,
            List(2000) { Point(it * 37 % 1000, it) },
            emptyList(),
        )

    @Test
    fun helloThenReady() =
        runTest {
            val (dash, session) = connect()
            val state = session.start()
            assertTrue(state is SessionState.Ready)
            assertEquals("0.5.0", (state as SessionState.Ready).dash.fw)
            assertEquals(HelloPhone("0.1.0", setOf(PhoneCap.CALLS)), dash.received.single())
            assertEquals(listOf(true), dash.writes.map { it.second }) // hello: write with response
            session.send(PhoneBattery(64, false))
            assertEquals(PhoneBattery(64, false), dash.received.last())
            assertEquals(false, dash.writes.last().second)
        }

    @Test
    fun framesWaitForCredit() =
        runTest {
            val (dash, session) = connect(FakeDash(window = 4))
            session.start() // the hello was frame 1, and the dash has consumed it
            val sending = launch { session.send(bigRoute()) }
            testScheduler.advanceUntilIdle()
            assertEquals(1 + 4, dash.writes.size) // a full window, then it waits
            dash.grant()
            testScheduler.advanceUntilIdle()
            assertEquals(1 + 8, dash.writes.size)
            while (sending.isActive) {
                dash.grant()
                testScheduler.advanceUntilIdle()
            }
            assertTrue(dash.received.last() is Route)
        }

    @Test
    fun framesStayWithinAndroidsWriteLimitAtTheLargestMtu() =
        runTest {
            val (dash, session) = connect(FakeDash(mtu = 517))
            session.start()
            val sending = launch { session.send(bigRoute()) }
            while (sending.isActive) {
                dash.grant()
                testScheduler.advanceUntilIdle()
            }
            assertTrue(dash.writes.all { (size, _) -> size <= MAX_WRITE_BYTES })
            assertTrue(dash.writes.any { (size, _) -> size == MAX_WRITE_BYTES })
            assertTrue(dash.received.last() is Route)
        }

    @Test
    fun closingReleasesAWaitingSender() =
        runTest {
            val (_, session) = connect(FakeDash(window = 2))
            session.start()
            val sending = async { runCatching { session.send(bigRoute()) } }
            testScheduler.advanceUntilIdle()
            session.close()
            assertTrue(sending.await().exceptionOrNull() is LinkClosedException)
        }

    @Test
    fun dashMessagesAreDelivered() =
        runTest {
            val (dash, session) = connect()
            session.start()
            val next = async { session.received.first() }
            testScheduler.advanceUntilIdle()
            dash.reply(CallAction(CallChoice.ACCEPT))
            assertEquals(CallAction(CallChoice.ACCEPT), next.await())
        }

    @Test
    fun newerDashMeansUpdateTheApp() =
        runTest {
            val (_, session) = connect(FakeDash(protoMajor = 2))
            val state = session.start()
            assertTrue(state is SessionState.Incompatible)
            assertTrue((state as SessionState.Incompatible).updateApp)
        }

    @Test
    fun badFramesAreCountedNotThrown() =
        runTest {
            val (_, session) = connect()
            session.onFrame(byteArrayOf(0x11, 0x01, 0x01)) // a continuation with nothing before it
            session.onFrame(byteArrayOf(1))
            assertEquals(2L, session.stats.value.dropped)
            assertEquals("short_frame", session.stats.value.lastDrop)
        }

    @Test(expected = IllegalStateException::class)
    fun nothingButHelloBeforeTheDashAnswers() =
        runTest {
            val (_, session) = connect()
            session.send(PhoneBattery(50, true))
        }

    @Test
    fun reconnectBacksOffAndResets() {
        val policy = ReconnectPolicy(firstMs = 1_000, maxMs = 8_000)
        assertEquals(listOf(1_000L, 2_000L, 4_000L, 8_000L, 8_000L), List(5) { policy.nextDelayMs() })
        policy.connected()
        assertEquals(1_000L, policy.nextDelayMs())
    }

    // A map chunk of about 3 KB: 13 frames at MTU 247.
    private fun bigChunk() =
        Chunk(
            ChunkKey(0, 11723, 7596, 20261101),
            Provenance.OSM_OPENFREEMAP,
            7,
            0,
            0,
            20,
            1200,
            List(
                60,
            ) { line -> Shape(5, List(20) { Point((line * 17 + it * 53) % 1200, (line * 31 + it * 7) % 1200) }) },
            emptyList(),
        )

    @Test
    fun otherMessagesGoBetweenABulkMessagesFrames() =
        runTest {
            val (dash, session) = connect(FakeDash(window = 2))
            session.start()
            val bulk = async { session.sendBulk(bigChunk()) }
            testScheduler.advanceUntilIdle() // two frames out, waiting for credit
            val battery = launch { session.send(PhoneBattery(50, true)) }
            while (bulk.isActive || battery.isActive) {
                dash.grant()
                testScheduler.advanceUntilIdle()
            }
            assertTrue(bulk.await())
            val order = dash.received.map { it::class.simpleName }
            assertTrue(order.indexOf("PhoneBattery") < order.indexOf("Chunk")) // it didn't wait for the chunk
            assertTrue(dash.drops.isEmpty())
        }

    @Test
    fun aMultiFrameMessageCutsABulkOneShort() =
        runTest {
            val (dash, session) = connect(FakeDash(window = 2))
            session.start()
            val bulk = async { session.sendBulk(bigChunk()) }
            testScheduler.advanceUntilIdle()
            val route = launch { session.send(bigRoute()) }
            while (bulk.isActive || route.isActive) {
                dash.grant()
                testScheduler.advanceUntilIdle()
            }
            assertFalse(bulk.await()) // send it again later
            assertTrue(dash.received.last() is Route)
            assertTrue(dash.received.none { it is Chunk })
            assertEquals(listOf("cut_short"), dash.drops)
        }

    @Test
    fun whatTheDashHoldsComesThrough() =
        runTest {
            val (dash, session) = connect()
            session.start()
            val have = async { session.chunkHave.first() }
            testScheduler.advanceUntilIdle()
            val sent = ChunkHave(7, listOf(ChunkKey(1, 2888, 1833, 20261018)))
            dash.reply(sent)
            assertEquals(sent, have.await())
        }
}
