package io.github.d4rthc0d3r.sarathi.link

import android.bluetooth.le.ScanFilter
import android.companion.AssociationRequest
import android.companion.BluetoothLeDeviceFilter
import android.companion.CompanionDeviceManager
import android.companion.ObservingDevicePresenceRequest
import android.content.Context
import android.content.IntentSender
import android.os.Build
import android.os.ParcelUuid
import androidx.annotation.RequiresApi
import androidx.core.content.edit
import io.github.d4rthc0d3r.sarathi.core.ble.DashGatt
import java.util.concurrent.Executor

/**
 * The phone's companion association with the dash (CompanionDeviceManager). The system's own
 * chooser finds the dash by its service UUID, so the app needs no Bluetooth scan or location
 * permission to find it, and Android keeps the association (no app storage).
 */
class DashAssociation(
    private val context: Context,
) {
    private val cdm = context.getSystemService(CompanionDeviceManager::class.java)
    private val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    /** The associated dash's Bluetooth address, or null before pairing. */
    fun address(): String? =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            cdm.myAssociations
                .firstOrNull()
                ?.deviceMacAddress
                ?.toString()
                ?.uppercase()
        } else {
            @Suppress("DEPRECATION") // the only way before Android 13
            cdm.associations.firstOrNull()?.uppercase()
        }

    private fun associationId(): Int? =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) cdm.myAssociations.firstOrNull()?.id else null

    /**
     * Opens the system's chooser for a nearby dash. [onChooser] gets the IntentSender to launch
     * (for an activity result); [onError] a message if it can't start.
     */
    fun request(
        executor: Executor,
        onChooser: (IntentSender) -> Unit,
        onError: (CharSequence?) -> Unit,
    ) {
        val filter =
            BluetoothLeDeviceFilter
                .Builder()
                .setScanFilter(ScanFilter.Builder().setServiceUuid(ParcelUuid(DashGatt.SERVICE)).build())
                .build()
        val request =
            AssociationRequest
                .Builder()
                .addDeviceFilter(filter)
                .setSingleDevice(true)
                .build()
        val callback =
            object : CompanionDeviceManager.Callback() {
                override fun onAssociationPending(intentSender: IntentSender) = onChooser(intentSender)

                @Deprecated("Android 12 and below call this one")
                override fun onDeviceFound(intentSender: IntentSender) = onChooser(intentSender)

                override fun onFailure(error: CharSequence?) = onError(error)
            }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            cdm.associate(request, executor, callback)
        } else {
            @Suppress("DEPRECATION") // the only way before Android 13
            cdm.associate(request, callback, null)
        }
    }

    /** Removes the association (and stops auto-start). Bonds are removed in Bluetooth settings. */
    fun forget() {
        setAutoStart(false)
        val address = address() ?: return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            associationId()?.let { cdm.disassociate(it) }
        } else {
            @Suppress("DEPRECATION")
            cdm.disassociate(address)
        }
    }

    /** Auto-start (optional): Android wakes the app when the dash appears nearby. Android 12+. */
    val autoStartSupported: Boolean get() = Build.VERSION.SDK_INT >= Build.VERSION_CODES.S

    val autoStart: Boolean get() = autoStartSupported && prefs.getBoolean(KEY_AUTO_START, false)

    /** Turns auto-start on or off; false if it isn't possible (no association, or Android < 12). */
    fun setAutoStart(enabled: Boolean): Boolean {
        val address = address()
        val changed =
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
                address != null &&
                runCatching { observe(address, enabled) }.isSuccess
        if (changed) prefs.edit { putBoolean(KEY_AUTO_START, enabled) }
        return changed
    }

    @RequiresApi(Build.VERSION_CODES.S) // only called when autoStartSupported
    private fun observe(
        address: String,
        enabled: Boolean,
    ) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.BAKLAVA) {
            val request =
                ObservingDevicePresenceRequest
                    .Builder()
                    .setAssociationId(
                        checkNotNull(associationId()),
                    ).build()
            if (enabled) cdm.startObservingDevicePresence(request) else cdm.stopObservingDevicePresence(request)
        } else {
            @Suppress("DEPRECATION") // replaced only from Android 16
            if (enabled) cdm.startObservingDevicePresence(address) else cdm.stopObservingDevicePresence(address)
        }
    }

    private companion object {
        const val PREFS = "dash"
        const val KEY_AUTO_START = "auto_start"
    }
}
