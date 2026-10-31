package io.github.d4rthc0d3r.sarathi.core.ble

import io.github.d4rthc0d3r.sarathi.core.protocol.CallAction
import io.github.d4rthc0d3r.sarathi.core.protocol.CallChoice
import io.github.d4rthc0d3r.sarathi.core.protocol.HelloPhone
import io.github.d4rthc0d3r.sarathi.core.protocol.PhoneBattery
import io.github.d4rthc0d3r.sarathi.core.protocol.PhoneCap
import io.github.d4rthc0d3r.sarathi.core.protocol.Point
import io.github.d4rthc0d3r.sarathi.core.protocol.Route
import io.github.d4rthc0d3r.sarathi.core.protocol.Routing
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
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
}
