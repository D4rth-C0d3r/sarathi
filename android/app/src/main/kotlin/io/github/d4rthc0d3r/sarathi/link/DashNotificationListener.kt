package io.github.d4rthc0d3r.sarathi.link

import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import io.github.d4rthc0d3r.sarathi.feature.calls.CallNotice
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Notification access (a settings switch the rider turns on). Music uses it to see the phone's
 * media sessions; calls use call notifications for the caller's name and, for internet calls,
 * their Answer and Decline buttons. Nothing else is read, and nothing is kept.
 */
class DashNotificationListener : NotificationListenerService() {
    override fun onListenerConnected() {
        activeNotifications
            .orEmpty()
            .mapNotNull {
                CallNotice.from(
                    it,
                )
            }.lastOrNull()
            ?.let { CallNotifications.post(it) }
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        CallNotice.from(sbn)?.let { CallNotifications.post(it) }
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) = CallNotifications.remove(sbn.key)
}

/** The latest call notification, for the calls feature. One per process. */
object CallNotifications {
    private val _current = MutableStateFlow<CallNotice?>(null)
    val current: StateFlow<CallNotice?> = _current.asStateFlow()

    fun post(notice: CallNotice) {
        _current.value = notice
    }

    fun remove(key: String) {
        if (_current.value?.key == key) _current.value = null
    }
}
