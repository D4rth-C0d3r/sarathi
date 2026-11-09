package io.github.d4rthc0d3r.sarathi.core.mapdata

import io.github.d4rthc0d3r.sarathi.core.protocol.ChunkAck
import io.github.d4rthc0d3r.sarathi.core.protocol.ChunkKey
import io.github.d4rthc0d3r.sarathi.core.protocol.RefuseReason

/**
 * What the dash's map cache holds, as far as the phone knows (ADR-0009): set from `chunk_have`
 * after each hello, then kept up to date from `chunk_ack` (stored, and protocol 1.2's evicted).
 * Versions don't matter here: a tile the dash has from another build isn't sent again. An L0 tile
 * sent as quarters (level 2) counts once all four are held.
 */
class HeldChunks {
    private val held = HashSet<Triple<Int, Long, Long>>()

    /** The ride frame these are in; a different one means the dash holds nothing we can use. */
    var epoch: Int = -1
        private set

    fun reset(
        epoch: Int,
        keys: List<ChunkKey> = emptyList(),
    ) {
        this.epoch = epoch
        held.clear()
        keys.forEach { held += it.id() }
    }

    /** Updates from an acknowledgement; returns the reasons for any refusals, by key. */
    fun onAck(ack: ChunkAck): Map<ChunkKey, RefuseReason> {
        ack.stored.forEach { held += it.id() }
        ack.evicted.forEach { held -= it.id() }
        return ack.refused.associate { it.key to it.reason }
    }

    fun holds(tile: TileId): Boolean {
        val x = tile.x.toLong()
        val y = tile.y.toLong()
        val quartered =
            tile.level == MapLevel.L0 &&
                QUARTERS.all { (qx, qy) -> Triple(QUARTER_LEVEL, 2 * x + qx, 2 * y + qy) in held }
        return Triple(tile.level.code, x, y) in held || quartered
    }

    val size: Int get() = held.size

    private fun ChunkKey.id() = Triple(level, tileX, tileY)

    private companion object {
        const val QUARTER_LEVEL = 2
        val QUARTERS = listOf(0 to 0, 1 to 0, 0 to 1, 1 to 1)
    }
}
