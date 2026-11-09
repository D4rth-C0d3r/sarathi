package io.github.d4rthc0d3r.sarathi.link

import android.content.Context
import android.util.Log
import io.github.d4rthc0d3r.sarathi.core.ble.DashChannel
import io.github.d4rthc0d3r.sarathi.core.mapdata.ChunkMaker
import io.github.d4rthc0d3r.sarathi.core.mapdata.Corridor
import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint
import io.github.d4rthc0d3r.sarathi.core.mapdata.HeldChunks
import io.github.d4rthc0d3r.sarathi.core.mapdata.MapLevel
import io.github.d4rthc0d3r.sarathi.core.mapdata.TileId
import io.github.d4rthc0d3r.sarathi.core.mapdata.TileShapes
import io.github.d4rthc0d3r.sarathi.core.maps.Route
import io.github.d4rthc0d3r.sarathi.core.protocol.BinaryCodec
import io.github.d4rthc0d3r.sarathi.core.protocol.Chunk
import io.github.d4rthc0d3r.sarathi.core.protocol.ChunkAck
import io.github.d4rthc0d3r.sarathi.core.protocol.ChunkKey
import io.github.d4rthc0d3r.sarathi.core.protocol.RefuseReason
import io.github.d4rthc0d3r.sarathi.core.protocol.RideFrame
import io.github.d4rthc0d3r.sarathi.maps.MapsRegistry
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull

/**
 * The map for the dash during a ride (ADR-0009, step G3): the tiles the corridor planner asks
 * for, nearest first, as chunks. One chunk in flight at a time, waiting for its `chunk_ack`;
 * chunks are bulk messages, so guidance and position go between their frames. What the dash
 * holds comes from its `chunk_have` after each hello, and from the acks (stored, evicted).
 */
class ChunkSender(
    context: Context,
    private val dash: DashChannel,
    private val scope: CoroutineScope,
    private val pose: PoseSender,
    private val route: StateFlow<Route?>,
) {
    private val maps = MapsRegistry.get(context)
    private val held = HeldChunks()
    private val acks = Channel<ChunkAck>(ACK_BUFFER)
    private val retryAt = HashMap<TileId, Long>()
    private var rider: GeoPoint? = null
    private var pausedUntil = 0L
    private val jobs = mutableListOf<Job>()

    fun start() {
        jobs += scope.launch { pose.fixes.collect { rider = GeoPoint(it.at.lat, it.at.lng) } }
        // Each hello says which frame the dash holds; none (a reset dash) or another one means
        // it holds nothing we sent. Its chunk_have, if any, refills the list.
        jobs +=
            scope.launch {
                dash.dashHello.collect { h ->
                    if (h != null && h.epoch != held.epoch) {
                        held.reset(h.epoch ?: -1)
                        Log.i(TAG, "map: the dash holds frame ${h.epoch}: starting over")
                    }
                }
            }
        jobs +=
            scope.launch {
                dash.chunkHave.collect {
                    held.reset(it.epoch, it.entries)
                    Log.i(TAG, "map: the dash holds ${it.entries.size} chunks (frame ${it.epoch})")
                }
            }
        jobs +=
            scope.launch {
                dash.received.collect { m ->
                    if (m is ChunkAck) {
                        held.onAck(m)
                        acks.trySend(m)
                    }
                }
            }
        jobs += scope.launch { run() }
    }

    fun stop() {
        jobs.forEach { it.cancel() }
        jobs.clear()
    }

    private suspend fun run() {
        while (true) {
            val next = nextTile()
            if (next == null) {
                delay(IDLE_MS)
            } else {
                sendTile(next.first, next.second)
            }
        }
    }

    /** The most urgent tile the dash lacks, with the frame to send it in; null if none now. */
    private fun nextTile(): Pair<TileId, RideFrame>? {
        val now = System.currentTimeMillis()
        val frame = pose.frame.value?.takeIf { canSend(now) }
        val here = rider
        val line = route.value?.points
        if (frame == null || here == null || line == null) return null
        if (held.epoch != frame.epoch) held.reset(frame.epoch) // a new frame: the dash dropped its chunks
        return Corridor
            .plan(line.map { GeoPoint(it.lat, it.lng) }, here)
            .firstOrNull { !held.holds(it) && (retryAt[it] ?: 0) <= now }
            ?.let { it to frame }
    }

    private fun canSend(now: Long) = dash.ready.value && (dash.dashHello.value?.chunkKb ?: 0) > 0 && now >= pausedUntil

    private suspend fun sendTile(
        tile: TileId,
        frame: RideFrame,
    ) {
        val now = System.currentTimeMillis()
        val result = maps.mapData.shapes(tile)
        val minor = dash.dashHello.value?.protoMinor ?: 0
        val chunks = result?.let { chunksFor(it.shapes, frame, minor) }
        when {
            result == null -> retryAt[tile] = now + NO_DATA_RETRY_MS

            // no source has it now
            chunks == null -> skip(tile, now + NEVER_MS, "too dense")

            !sendAll(chunks) -> skip(tile, now + CUT_RETRY_MS, "cut short or link down")

            else -> afterSend(tile, "${result.origin}", chunks, now)
        }
    }

    private suspend fun sendAll(chunks: List<Chunk>): Boolean {
        while (true) acks.tryReceive().getOrNull() ?: break // older acks (evictions) are already counted
        return chunks.all { runCatching { dash.sendBulk(it) }.getOrDefault(false) }
    }

    private suspend fun afterSend(
        tile: TileId,
        origin: String,
        chunks: List<Chunk>,
        now: Long,
    ) {
        val outcome = awaitAck(chunks.map { it.key })
        Log.i(
            TAG,
            "map: ${tile.level} ${tile.x}/${tile.y} from $origin, ${chunks.size} chunk(s): ${outcome ?: "stored"}",
        )
        when (outcome) {
            RefuseReason.FULL -> pausedUntil = now + FULL_PAUSE_MS
            RefuseReason.INVALID -> retryAt[tile] = now + NEVER_MS
            RefuseReason.EPOCH -> held.reset(-1)
            null -> if (!held.holds(tile)) retryAt[tile] = now + CUT_RETRY_MS // no ack: try again
        }
    }

    /** The first refusal among [keys], null when all were stored or no ack came in time. */
    private suspend fun awaitAck(keys: List<ChunkKey>): RefuseReason? =
        withTimeoutOrNull(ACK_TIMEOUT_MS) {
            val pending = keys.toMutableSet()
            var refusal: RefuseReason? = null
            while (pending.isNotEmpty()) {
                val ack = acks.receive()
                pending -= ack.stored.toSet()
                ack.refused.filter { it.key in pending }.forEach {
                    pending -= it.key
                    refusal = refusal ?: it.reason
                }
            }
            refusal
        }

    private fun skip(
        tile: TileId,
        until: Long,
        why: String,
    ) {
        retryAt[tile] = until
        Log.i(TAG, "map: ${tile.level} ${tile.x}/${tile.y} not sent ($why)")
    }

    private companion object {
        const val TAG = "Map"
        const val IDLE_MS = 3_000L
        const val ACK_TIMEOUT_MS = 10_000L
        const val CUT_RETRY_MS = 5_000L
        const val NO_DATA_RETRY_MS = 120_000L
        const val FULL_PAUSE_MS = 60_000L
        const val NEVER_MS = 3_600_000L
        const val ACK_BUFFER = 16
    }
}

/** One chunk, or an L0 tile's four quarters if it's too dense and the dash takes them (1.2). */
private fun chunksFor(
    shapes: TileShapes,
    frame: RideFrame,
    dashMinor: Int,
): List<Chunk>? {
    val whole = runCatching { ChunkMaker.chunk(shapes, frame, frame.epoch).also { BinaryCodec.encode(it) } }
    val quarters = dashMinor >= QUARTERS_SINCE_MINOR && shapes.level == MapLevel.L0
    return when {
        whole.isSuccess -> {
            listOf(whole.getOrThrow())
        }

        quarters -> {
            runCatching {
                ChunkMaker.quarters(shapes, frame, frame.epoch).onEach { BinaryCodec.encode(it) }
            }.getOrNull()
        }

        else -> {
            null
        }
    }
}

private const val QUARTERS_SINCE_MINOR = 2 // protocol 1.2
