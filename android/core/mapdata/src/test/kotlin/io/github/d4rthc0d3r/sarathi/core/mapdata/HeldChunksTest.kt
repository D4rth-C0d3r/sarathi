package io.github.d4rthc0d3r.sarathi.core.mapdata

import io.github.d4rthc0d3r.sarathi.core.protocol.ChunkAck
import io.github.d4rthc0d3r.sarathi.core.protocol.ChunkKey
import io.github.d4rthc0d3r.sarathi.core.protocol.RefuseReason
import io.github.d4rthc0d3r.sarathi.core.protocol.RefusedChunk
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class HeldChunksTest {
    private val l0 = TileId(MapLevel.L0, 11558, 7476)
    private val l1 = TileId(MapLevel.L1, 2889, 1869)

    @Test
    fun followsHaveStoredAndEvicted() {
        val held = HeldChunks()
        held.reset(7, listOf(ChunkKey(1, 2889, 1869, 20261101)))
        assertTrue(held.holds(l1))
        assertFalse(held.holds(l0))
        val refused =
            held.onAck(
                ChunkAck(
                    stored = listOf(ChunkKey(0, 11558, 7476, 20261109)), // another version: still held
                    refused = listOf(RefusedChunk(ChunkKey(0, 1, 1, 1), RefuseReason.FULL)),
                    evicted = listOf(ChunkKey(1, 2889, 1869, 20261101)),
                ),
            )
        assertTrue(held.holds(l0))
        assertFalse(held.holds(l1))
        assertEquals(mapOf(ChunkKey(0, 1, 1, 1) to RefuseReason.FULL), refused)
        held.reset(8) // a new frame: nothing
        assertFalse(held.holds(l0))
        assertEquals(8, held.epoch)
    }

    @Test
    fun anL0TileIsHeldOnceAllItsQuartersAre() {
        val held = HeldChunks()
        held.reset(7)
        val quarters = listOf(0 to 0, 1 to 0, 0 to 1).map { (qx, qy) -> ChunkKey(2, 2L * l0.x + qx, 2L * l0.y + qy, 1) }
        held.onAck(ChunkAck(quarters, emptyList()))
        assertFalse(held.holds(l0))
        held.onAck(ChunkAck(listOf(ChunkKey(2, 2L * l0.x + 1, 2L * l0.y + 1, 1)), emptyList()))
        assertTrue(held.holds(l0))
    }
}
