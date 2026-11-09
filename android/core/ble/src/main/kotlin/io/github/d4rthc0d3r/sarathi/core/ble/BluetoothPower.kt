package io.github.d4rthc0d3r.sarathi.core.ble

import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.async
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch

/** Whether Bluetooth is on, as Android announces it; starts with the state now. */
internal fun bluetoothOn(context: Context): Flow<Boolean> =
    callbackFlow {
        trySend(context.getSystemService(BluetoothManager::class.java)?.adapter?.isEnabled == true)
        val receiver =
            object : BroadcastReceiver() {
                override fun onReceive(
                    c: Context,
                    intent: Intent,
                ) {
                    // Turning on or off counts as off: nothing can connect until it's on.
                    trySend(
                        intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, BluetoothAdapter.ERROR) ==
                            BluetoothAdapter.STATE_ON,
                    )
                }
            }
        context.registerReceiver(receiver, IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED))
        awaitClose { context.unregisterReceiver(receiver) }
    }.distinctUntilChanged()

/**
 * Runs [block], cancelling it if Bluetooth goes off meanwhile; then the result is [whenOff].
 * Android drops a waiting connection when Bluetooth goes off, so waiting on would never end.
 */
internal suspend fun <T> untilBluetoothOff(
    bluetooth: Flow<Boolean>,
    whenOff: () -> T,
    block: suspend () -> T,
): T =
    coroutineScope {
        val attempt = async { block() }
        val watch =
            launch {
                bluetooth.first { !it }
                attempt.cancel()
            }
        try {
            attempt.await()
        } catch (
            @Suppress("SwallowedException") e: CancellationException, // our own cancel: Bluetooth went off
        ) {
            currentCoroutineContext().ensureActive() // rethrows if the link itself is stopping
            whenOff()
        } finally {
            watch.cancel()
        }
    }
