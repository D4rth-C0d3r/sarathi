package io.github.d4rthc0d3r.sarathi.feature.calls

import io.github.d4rthc0d3r.sarathi.core.protocol.Call
import io.github.d4rthc0d3r.sarathi.core.protocol.CallPhase
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CallPolicyTest {
    private val ringingNotice = CallNoticeInfo("Amma", incoming = true)

    @Test
    fun cellularCallsFollowTheCallState() {
        assertEquals(Call(CallPhase.RINGING, "Amma"), CallPolicy.message(Telephony.RINGING, ringingNotice))
        assertEquals(Call(CallPhase.RINGING, ""), CallPolicy.message(Telephony.RINGING, null)) // name arrives next
        assertEquals(Call(CallPhase.ACTIVE, "Amma"), CallPolicy.message(Telephony.OFFHOOK, ringingNotice))
        assertEquals(Call(CallPhase.IDLE, ""), CallPolicy.message(Telephony.IDLE, null))
        assertFalse(CallPolicy.isInternetCall(Telephony.RINGING, ringingNotice))
    }

    @Test
    fun internetCallsFollowTheirNotification() {
        assertEquals(Call(CallPhase.RINGING, "Amma"), CallPolicy.message(Telephony.IDLE, ringingNotice))
        assertEquals(
            Call(CallPhase.ACTIVE, "Amma"),
            CallPolicy.message(Telephony.IDLE, ringingNotice.copy(incoming = false)),
        )
        assertTrue(CallPolicy.isInternetCall(Telephony.IDLE, ringingNotice))
    }

    @Test
    fun namesAreDashText() {
        assertEquals("Jose Garcia", CallPolicy.message(Telephony.RINGING, CallNoticeInfo("José García", true)).caller)
        assertEquals(32, CallPolicy.message(Telephony.RINGING, CallNoticeInfo("x".repeat(60), true)).caller.length)
    }
}
