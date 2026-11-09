package io.github.d4rthc0d3r.sarathi.core.ble

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class BluetoothPowerTest {
    @Test
    fun aWaitingAttemptEndsWhenBluetoothGoesOff() =
        runTest {
            val bluetooth = MutableStateFlow(true)
            val never = CompletableDeferred<String>()
            val result = async { untilBluetoothOff(bluetooth, { "off" }) { never.await() } }
            runCurrent()
            bluetooth.value = false
            runCurrent()
            assertEquals("off", result.await())
            assertTrue(never.isActive) // the attempt was left, not completed
        }

    @Test
    fun aFinishedAttemptKeepsItsResult() =
        runTest {
            val bluetooth = MutableStateFlow(true)
            assertEquals("done", untilBluetoothOff(bluetooth, { "off" }) { "done" })
        }
}
