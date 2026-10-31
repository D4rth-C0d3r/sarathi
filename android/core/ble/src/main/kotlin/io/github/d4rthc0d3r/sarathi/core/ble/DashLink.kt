package io.github.d4rthc0d3r.sarathi.core.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.Context
import io.github.d4rthc0d3r.sarathi.core.protocol.BinaryMessage
import io.github.d4rthc0d3r.sarathi.core.protocol.JsonMessage
import io.github.d4rthc0d3r.sarathi.core.protocol.PhoneCap
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * The connection to one dash, by Bluetooth address: connect, bond, set up, run a
 * [PhoneLinkSession], and reconnect for as long as it runs. The Bluetooth stack does the waiting
 * (autoConnect), so nothing polls and Doze doesn't stall it; only failures back off.
 */
@SuppressLint("MissingPermission") // every call needs BLUETOOTH_CONNECT; runOnce() checks it first
class DashLink(
    private val context: Context,
    private val address: String,
    private val appVersion: String,
    private val scope: CoroutineScope,
    private val caps: Set<PhoneCap> = emptySet(),
) : DashChannel {
    private val _state = MutableStateFlow<LinkState>(LinkState.Stopped)
    val state: StateFlow<LinkState> = _state.asStateFlow()

    private val _received = MutableSharedFlow<JsonMessage>(extraBufferCapacity = BUFFER)
    override val received: SharedFlow<JsonMessage> = _received.asSharedFlow()

    private val _ready = MutableStateFlow(false)
    override val ready: StateFlow<Boolean> = _ready.asStateFlow()

    init {
        scope.launch { state.collect { _ready.value = it is LinkState.Ready } }
    }

    private var job: Job? = null
    private var session: PhoneLinkSession? = null
    private val policy = ReconnectPolicy()

    fun start() {
        if (job?.isActive == true) return
        job = scope.launch { runForever() }
    }

    fun stop() {
        job?.cancel()
        job = null
        session?.close()
        _state.value = LinkState.Stopped
    }

    /** Sends to the dash once it's ready; throws [LinkClosedException] or IllegalStateException if not. */
    override suspend fun send(message: JsonMessage) = checkNotNull(session) { "not connected" }.send(message)

    override suspend fun send(message: BinaryMessage) = checkNotNull(session) { "not connected" }.send(message)

    private suspend fun runForever() {
        var earlyDrops = 0
        while (scope.isActive) {
            val failure = runOnce()
            if (failure == null) {
                earlyDrops = 0
                _state.value = LinkState.Waiting // a plain disconnect: wait for the dash again
                continue
            }
            // A bonded phone the dash forgot (Forget phones) is dropped as soon as it encrypts.
            earlyDrops = if (failure.detail == DROPPED_DETAIL) earlyDrops + 1 else 0
            _state.value =
                if (earlyDrops >= STALE_AFTER_DROPS) {
                    LinkState.Failed(LinkProblem.STALE_PAIRING, "the dash keeps dropping this phone")
                } else {
                    failure
                }
            delay(policy.nextDelayMs())
        }
    }

    /** One connection, from waiting to disconnect. Null for a plain disconnect. */
    private suspend fun runOnce(): LinkState.Failed? {
        val adapter = context.getSystemService(BluetoothManager::class.java)?.adapter
        return when {
            !DashGatt.canConnect(context) -> {
                LinkState.Failed(LinkProblem.NO_PERMISSION, "Nearby devices not allowed")
            }

            adapter == null || !adapter.isEnabled -> {
                LinkState.Failed(LinkProblem.BLUETOOTH_OFF, "Bluetooth is off")
            }

            else -> {
                val device = adapter.getRemoteDevice(address)
                try {
                    GattConnection(context, device).use { connect(it, device) }
                } catch (e: SetupFailure) {
                    e.state
                } finally {
                    session?.close()
                    session = null
                }
            }
        }
    }

    private suspend fun connect(
        connection: GattConnection,
        device: BluetoothDevice,
    ): LinkState.Failed? {
        _state.value = LinkState.Waiting
        if (!connection.awaitConnected()) fail(LinkProblem.OTHER, "connect failed")
        val bonded = device.bondState == BluetoothDevice.BOND_BONDED
        if (!bonded) _state.value = LinkState.Pairing
        if (!bonded && !bondWith(context, device, PAIRING_TIMEOUT_MS)) {
            fail(LinkProblem.PAIRING_FAILED, "pairing refused or timed out")
        }
        _state.value = LinkState.Connecting
        val mtu = connection.raiseMtu()
        if (mtu < MIN_MTU) fail(LinkProblem.SETUP_FAILED, "MTU $mtu, below $MIN_MTU")
        var (rx, tx) = connection.discover()
        if (!connection.enableNotifications(tx)) {
            // The services changed (a firmware change at this address): look again, once.
            val again = connection.discover()
            rx = again.first
            if (!connection.enableNotifications(again.second)) fail(LinkProblem.SETUP_FAILED, "services keep changing")
        }
        return runSession(connection, PhoneLinkSession(connection.port(rx, mtu), appVersion, caps))
    }

    /**
     * Runs the protocol session until the link drops (null), or until the dash fails to answer
     * hello (that failure: the connection is closed and retried after a pause).
     */
    private suspend fun runSession(
        connection: GattConnection,
        link: PhoneLinkSession,
    ): LinkState.Failed? {
        session = link
        val ended = CompletableDeferred<LinkState.Failed?>()
        // Frames go to the session from their own coroutine, and connection events from theirs,
        // so a disconnect is seen even while a send waits for credit.
        val jobs =
            listOf(
                scope.launch { for (frame in connection.frames) link.onFrame(frame) },
                scope.launch { link.received.collect { _received.emit(it) } },
                scope.launch {
                    val state = started(link)
                    _state.value = state
                    if (state is LinkState.Failed) ended.complete(state)
                },
                scope.launch {
                    connection.awaitDisconnect()
                    ended.complete(null)
                },
            )
        try {
            return ended.await()
        } finally {
            link.close()
            jobs.forEach { it.cancel() }
        }
    }

    private suspend fun started(link: PhoneLinkSession): LinkState =
        when (val result = runCatching { link.start() }.getOrElse { SessionState.Closed }) {
            is SessionState.Ready -> {
                policy.connected()
                LinkState.Ready(result.dash)
            }

            is SessionState.Incompatible -> {
                LinkState.Incompatible(result.dash, result.updateApp)
            }

            else -> {
                LinkState.Failed(LinkProblem.SETUP_FAILED, "the dash didn't answer hello")
            }
        }

    private fun fail(
        problem: LinkProblem,
        detail: String,
    ): Nothing = throw SetupFailure(LinkState.Failed(problem, detail))

    private companion object {
        const val BUFFER = 32
        const val MIN_MTU = 247
        const val PAIRING_TIMEOUT_MS = 60_000L
        const val STALE_AFTER_DROPS = 3
    }
}
