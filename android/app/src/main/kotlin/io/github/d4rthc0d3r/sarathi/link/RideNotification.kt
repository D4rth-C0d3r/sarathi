package io.github.d4rthc0d3r.sarathi.link

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import androidx.core.app.NotificationCompat
import io.github.d4rthc0d3r.sarathi.MainActivity
import io.github.d4rthc0d3r.sarathi.R
import io.github.d4rthc0d3r.sarathi.core.ble.LinkState

/** The ride link's ongoing notification: the link state, a tap opens the app, Stop ends it. */
object RideNotification {
    const val ID = 1
    private const val CHANNEL = "ride_link"

    fun build(
        context: Context,
        state: LinkState,
        stop: Intent,
    ): Notification {
        context.getSystemService(NotificationManager::class.java).createNotificationChannel(
            NotificationChannel(
                CHANNEL,
                context.getString(R.string.channel_ride_link),
                NotificationManager.IMPORTANCE_LOW,
            ),
        )
        val open =
            PendingIntent.getActivity(
                context,
                0,
                Intent(context, MainActivity::class.java),
                PendingIntent.FLAG_IMMUTABLE,
            )
        return NotificationCompat
            .Builder(context, CHANNEL)
            .setSmallIcon(R.drawable.ic_stat_link)
            .setContentTitle(context.getString(R.string.app_name))
            .setContentText(context.getString(LinkText.of(state)))
            .setOngoing(true)
            .setContentIntent(open)
            .addAction(
                0,
                context.getString(R.string.action_stop),
                PendingIntent.getService(context, 1, stop, PendingIntent.FLAG_IMMUTABLE),
            ).setForegroundServiceBehavior(NotificationCompat.FOREGROUND_SERVICE_IMMEDIATE)
            .build()
    }
}
