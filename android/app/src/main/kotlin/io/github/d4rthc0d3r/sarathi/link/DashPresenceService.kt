package io.github.d4rthc0d3r.sarathi.link

import android.companion.AssociationInfo
import android.companion.CompanionDeviceService
import android.companion.DevicePresenceEvent
import android.os.Build
import androidx.annotation.RequiresApi

/**
 * Android binds this when the associated dash appears or disappears nearby, if auto-start is on
 * (Android 12+). The ride link starts even with the phone locked: companion apps may start a
 * foreground service from the background (ADR-0010).
 */
@RequiresApi(Build.VERSION_CODES.S)
class DashPresenceService : CompanionDeviceService() {
    @RequiresApi(Build.VERSION_CODES.BAKLAVA)
    override fun onDevicePresenceEvent(event: DevicePresenceEvent) {
        when (event.event) {
            DevicePresenceEvent.EVENT_BLE_APPEARED -> RideLinkService.start(this, fromPresence = true)
            DevicePresenceEvent.EVENT_BLE_DISAPPEARED -> RideLinkService.presenceGone(this)
        }
    }

    @Deprecated("Android 13-15 call this one")
    override fun onDeviceAppeared(associationInfo: AssociationInfo) = RideLinkService.start(this, fromPresence = true)

    @Deprecated("Android 13-15 call this one")
    override fun onDeviceDisappeared(associationInfo: AssociationInfo) = RideLinkService.presenceGone(this)

    @Deprecated("Android 12 calls this one")
    override fun onDeviceAppeared(address: String) = RideLinkService.start(this, fromPresence = true)

    @Deprecated("Android 12 calls this one")
    override fun onDeviceDisappeared(address: String) = RideLinkService.presenceGone(this)
}
