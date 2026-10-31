package io.github.d4rthc0d3r.sarathi.core.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.withTimeoutOrNull

/**
 * Bonds with the dash. Android shows its pairing dialog with a 6-digit code; the dash shows the
 * same code and the rider confirms there with the knob (SECURITY.md). True once bonded.
 */
@SuppressLint("MissingPermission") // callers hold BLUETOOTH_CONNECT (DashGatt.canConnect)
internal suspend fun bondWith(
    context: Context,
    device: BluetoothDevice,
    timeoutMs: Long,
): Boolean {
    val states = Channel<Int>(Channel.UNLIMITED)
    val receiver =
        object : BroadcastReceiver() {
            override fun onReceive(
                context: Context,
                intent: Intent,
            ) {
                if (intent.device()?.address == device.address) {
                    states.trySend(intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.ERROR))
                }
            }
        }
    context.registerReceiver(receiver, IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED))
    try {
        if (!device.createBond()) return device.bondState == BluetoothDevice.BOND_BONDED
        val end =
            withTimeoutOrNull(timeoutMs) {
                states.receiveAsFlow().first { it == BluetoothDevice.BOND_BONDED || it == BluetoothDevice.BOND_NONE }
            }
        return end == BluetoothDevice.BOND_BONDED
    } finally {
        context.unregisterReceiver(receiver)
    }
}

private fun Intent.device(): BluetoothDevice? =
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
    } else {
        @Suppress("DEPRECATION") // the only way before Android 13
        getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
    }
