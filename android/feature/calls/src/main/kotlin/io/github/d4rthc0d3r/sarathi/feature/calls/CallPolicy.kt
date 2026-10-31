package io.github.d4rthc0d3r.sarathi.feature.calls

import io.github.d4rthc0d3r.sarathi.core.protocol.Call
import io.github.d4rthc0d3r.sarathi.core.protocol.CallPhase
import io.github.d4rthc0d3r.sarathi.core.protocol.DashText

/** The phone's cellular call state (TelephonyManager). */
enum class Telephony { IDLE, RINGING, OFFHOOK }

/** A call notification as the policy sees it: who, and whether it's still ringing. */
data class CallNoticeInfo(
    val name: String,
    val incoming: Boolean,
)

/**
 * What the dash shows for calls (protocol.md section 3, `call`). A cellular call's phase comes
 * from the phone's call state; an internet call (WhatsApp and the like) never changes that, so
 * its phase comes from its call notification. The name is the notification's, as the phone shows
 * it: a contact name, or the number.
 */
object CallPolicy {
    fun message(
        telephony: Telephony,
        notice: CallNoticeInfo?,
    ): Call {
        val phase =
            when (telephony) {
                Telephony.RINGING -> {
                    CallPhase.RINGING
                }

                Telephony.OFFHOOK -> {
                    CallPhase.ACTIVE
                }

                Telephony.IDLE -> {
                    when {
                        notice == null -> CallPhase.IDLE
                        notice.incoming -> CallPhase.RINGING
                        else -> CallPhase.ACTIVE
                    }
                }
            }
        val caller = if (phase == CallPhase.IDLE) "" else DashText.of(notice?.name.orEmpty(), Call.CALLER_BYTES)
        return Call(phase, caller)
    }

    /** True if the call on the dash is an internet call (handled through its notification). */
    fun isInternetCall(
        telephony: Telephony,
        notice: CallNoticeInfo?,
    ) = telephony == Telephony.IDLE && notice != null
}
