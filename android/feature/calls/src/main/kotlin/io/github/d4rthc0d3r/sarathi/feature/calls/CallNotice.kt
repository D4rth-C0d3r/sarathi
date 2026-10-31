package io.github.d4rthc0d3r.sarathi.feature.calls

import android.app.Notification
import android.app.PendingIntent
import android.app.Person
import android.os.Build
import android.service.notification.StatusBarNotification

/**
 * A call notification (Notification.CallStyle, or category "call"): the caller as the phone
 * shows it, and the buttons to press for an internet call. CallStyle puts decline first and
 * answer last for an incoming call, and one hang-up button on an ongoing one.
 */
class CallNotice(
    val key: String,
    val info: CallNoticeInfo,
    val answer: PendingIntent?,
    val decline: PendingIntent?,
) {
    companion object {
        /** The call in [sbn], or null if it isn't a call notification. */
        fun from(sbn: StatusBarNotification): CallNotice? {
            val n = sbn.notification
            val extras = n.extras
            val type =
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    extras.getInt(Notification.EXTRA_CALL_TYPE, CALL_TYPE_NONE)
                } else {
                    CALL_TYPE_NONE
                }
            if (type == CALL_TYPE_NONE && n.category != Notification.CATEGORY_CALL) return null
            val incoming =
                if (type ==
                    CALL_TYPE_NONE
                ) {
                    n.actions.orEmpty().size >= 2
                } else {
                    type == Notification.CallStyle.CALL_TYPE_INCOMING
                }
            val actions = n.actions.orEmpty()
            return CallNotice(
                key = sbn.key,
                info = CallNoticeInfo(caller(extras), incoming),
                answer = if (incoming && actions.size >= 2) actions.last().actionIntent else null,
                decline = actions.firstOrNull()?.actionIntent,
            )
        }

        private fun caller(extras: android.os.Bundle): String {
            val name: CharSequence? =
                when {
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU -> {
                        extras.getParcelable(Notification.EXTRA_CALL_PERSON, Person::class.java)?.name
                    }

                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.S -> {
                        @Suppress("DEPRECATION") // the only way on Android 12
                        extras.getParcelable<Person>(Notification.EXTRA_CALL_PERSON)?.name
                    }

                    else -> {
                        null
                    }
                }
            return (name ?: extras.getCharSequence(Notification.EXTRA_TITLE))?.toString().orEmpty()
        }

        private const val CALL_TYPE_NONE = 0
    }
}
