package io.github.d4rthc0d3r.sarathi.link

import android.service.notification.NotificationListenerService

/**
 * Notification access (a settings switch the rider turns on). Music needs it to see the phone's
 * media sessions; nothing here reads other apps' notifications.
 */
class DashNotificationListener : NotificationListenerService()
