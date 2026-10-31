package io.github.d4rthc0d3r.sarathi.core.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.content.Context
import android.os.Build
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.withTimeoutOrNull

/** A setup step failed; [state] is what the link shows. */
internal class SetupFailure(
    val state: LinkState.Failed,
) : Exception(state.detail)

/** Thrown when the link drops during setup. */
internal const val DROPPED_DETAIL = "the link dropped during setup"

/**
 * One Bluetooth connection to the dash, from connectGatt to close. Callbacks are copied into
 * channels (nothing runs on Binder threads); each setup step waits for its own callback and
 * throws [SetupFailure] if the link drops or the step fails.
 */
@SuppressLint("MissingPermission") // callers hold BLUETOOTH_CONNECT (DashGatt.canConnect)
internal class GattConnection(
    context: Context,
    device: BluetoothDevice,
) : AutoCloseable {
    private val events = Channel<Event>(Channel.UNLIMITED)

    /** Notifications from `tx`, in order. */
    val frames = Channel<ByteArray>(Channel.UNLIMITED)

    /** Write completions (GATT status), for [Port]. */
    private val writes = Channel<Int>(Channel.UNLIMITED)

    // autoConnect: the Bluetooth stack waits for the dash and connects when it advertises. Its
    // replacement (BluetoothGattConnectionSettings) exists only from Android 17.
    @Suppress("DEPRECATION")
    private val gatt: BluetoothGatt? = device.connectGatt(context, true, Callback(), BluetoothDevice.TRANSPORT_LE)

    /** Suspends until the dash connects; false if the attempt failed. */
    suspend fun awaitConnected(): Boolean =
        gatt != null &&
            events.receiveAsFlow().first { it is Event.Connection }.let { (it as Event.Connection).connected }

    /** Asks for a 517-byte MTU; returns what was agreed. */
    suspend fun raiseMtu(): Int {
        checkNotNull(gatt).requestMtu(DashGatt.WANTED_MTU)
        return await<Event.Mtu>().mtu
    }

    /**
     * Finds the dash's service; returns `rx` and `tx`. Android answers from its cache for a
     * bonded device, which can predate a firmware change at this address (the Chronos build);
     * the dash then sends Service Changed, and the second look finds the service.
     */
    suspend fun discover(): Pair<BluetoothGattCharacteristic, BluetoothGattCharacteristic> {
        val g = checkNotNull(gatt)
        g.discoverServices()
        await<Event.Services>()
        if (g.getService(DashGatt.SERVICE) == null) {
            withTimeoutOrNull(SERVICE_CHANGED_WAIT_MS) { await<Event.ServiceChanged>() }
            g.discoverServices()
            await<Event.Services>()
        }
        val service = g.getService(DashGatt.SERVICE) ?: throw setupFailed("no sarathi service")
        val rx = service.getCharacteristic(DashGatt.RX)
        val tx = service.getCharacteristic(DashGatt.TX)
        if (rx == null || tx == null) throw setupFailed("characteristics missing")
        return rx to tx
    }

    /**
     * Turns on notifications from `tx`. False if the dash said its services changed meanwhile
     * (Android then re-reads them): discover again and retry.
     */
    suspend fun enableNotifications(tx: BluetoothGattCharacteristic): Boolean {
        val g = checkNotNull(gatt)
        g.setCharacteristicNotification(tx, true)
        val descriptor = tx.getDescriptor(DashGatt.CCCD) ?: throw setupFailed("no notification descriptor")
        val value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            g.writeDescriptor(descriptor, value)
        } else {
            @Suppress("DEPRECATION") // the only way before Android 13
            descriptor.value = value
            @Suppress("DEPRECATION")
            g.writeDescriptor(descriptor)
        }
        val event = await<Event>(setOf(Event.Descriptor::class, Event.ServiceChanged::class))
        val status = (event as? Event.Descriptor)?.status ?: return false
        if (status != BluetoothGatt.GATT_SUCCESS) throw setupFailed("notifications not enabled ($status)")
        return true
    }

    /** After setup: suspends until the link drops, passing write completions to [port]. */
    suspend fun awaitDisconnect() {
        while (true) {
            when (val event = events.receive()) {
                is Event.Connection -> if (!event.connected) return
                is Event.Write -> writes.trySend(event.status)
                else -> Unit
            }
        }
    }

    fun port(
        rx: BluetoothGattCharacteristic,
        mtu: Int,
    ): GattPort = Port(checkNotNull(gatt), rx, mtu, writes)

    override fun close() {
        gatt?.close()
    }

    /** Waits for the first event of any of [kinds]; throws [SetupFailure] on a drop or timeout. */
    private suspend fun <T : Event> await(kinds: Set<kotlin.reflect.KClass<out T>>): T =
        withTimeoutOrNull(SETUP_TIMEOUT_MS) {
            val event =
                events.receiveAsFlow().first { e ->
                    kinds.any { it.isInstance(e) } ||
                        (e is Event.Connection && !e.connected)
                }
            @Suppress("UNCHECKED_CAST") // matched one of kinds just above
            if (event is Event.Connection) null else event as T
        } ?: throw SetupFailure(LinkState.Failed(LinkProblem.OTHER, DROPPED_DETAIL))

    private suspend inline fun <reified T : Event> await(): T =
        withTimeoutOrNull(SETUP_TIMEOUT_MS) {
            val event = events.receiveAsFlow().first { it is T || (it is Event.Connection && !it.connected) }
            event as? T
        } ?: throw SetupFailure(LinkState.Failed(LinkProblem.OTHER, DROPPED_DETAIL))

    private fun setupFailed(detail: String) = SetupFailure(LinkState.Failed(LinkProblem.SETUP_FAILED, detail))

    /** Writes frames to `rx`, one at a time, each waiting for the stack's write callback. */
    private class Port(
        private val gatt: BluetoothGatt,
        private val rx: BluetoothGattCharacteristic,
        override val mtu: Int,
        private val completions: Channel<Int>,
    ) : GattPort {
        override suspend fun write(
            frame: ByteArray,
            withResponse: Boolean,
        ) {
            val type =
                if (withResponse) {
                    BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                } else {
                    BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
                }
            repeat(BUSY_RETRIES) {
                if (submit(frame, type)) {
                    val status = withTimeout(WRITE_TIMEOUT_MS) { completions.receive() }
                    check(status == BluetoothGatt.GATT_SUCCESS) { "write failed ($status)" }
                    return
                }
                delay(BUSY_DELAY_MS) // the stack's queue is full; it drains within milliseconds
            }
            throw LinkClosedException()
        }

        private fun submit(
            frame: ByteArray,
            type: Int,
        ): Boolean =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                gatt.writeCharacteristic(rx, frame, type) == BluetoothStatusCodes.SUCCESS
            } else {
                @Suppress("DEPRECATION") // the only way before Android 13
                rx.writeType = type
                @Suppress("DEPRECATION")
                rx.value = frame
                @Suppress("DEPRECATION")
                gatt.writeCharacteristic(rx)
            }
    }

    private sealed interface Event {
        data class Connection(
            val connected: Boolean,
        ) : Event

        data class Mtu(
            val mtu: Int,
        ) : Event

        data object Services : Event

        data object ServiceChanged : Event

        data class Descriptor(
            val status: Int,
        ) : Event

        data class Write(
            val status: Int,
        ) : Event
    }

    private inner class Callback : BluetoothGattCallback() {
        override fun onConnectionStateChange(
            gatt: BluetoothGatt,
            status: Int,
            newState: Int,
        ) {
            events.trySend(Event.Connection(newState == BluetoothProfile.STATE_CONNECTED))
        }

        override fun onMtuChanged(
            gatt: BluetoothGatt,
            mtu: Int,
            status: Int,
        ) {
            events.trySend(Event.Mtu(if (status == BluetoothGatt.GATT_SUCCESS) mtu else 0))
        }

        override fun onServicesDiscovered(
            gatt: BluetoothGatt,
            status: Int,
        ) {
            events.trySend(Event.Services)
        }

        override fun onServiceChanged(gatt: BluetoothGatt) {
            events.trySend(Event.ServiceChanged)
        }

        override fun onDescriptorWrite(
            gatt: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int,
        ) {
            events.trySend(Event.Descriptor(status))
        }

        override fun onCharacteristicWrite(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            events.trySend(Event.Write(status))
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            frames.trySend(value.copyOf())
        }

        @Deprecated("Android 12 and below call this one instead")
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
        ) {
            @Suppress("DEPRECATION")
            frames.trySend(characteristic.value.copyOf())
        }
    }

    private companion object {
        const val SETUP_TIMEOUT_MS = 10_000L
        const val SERVICE_CHANGED_WAIT_MS = 2_000L
        const val WRITE_TIMEOUT_MS = 5_000L
        const val BUSY_RETRIES = 50
        const val BUSY_DELAY_MS = 10L
    }
}
