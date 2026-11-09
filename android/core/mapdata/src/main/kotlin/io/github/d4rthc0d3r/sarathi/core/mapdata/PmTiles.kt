package io.github.d4rthc0d3r.sarathi.core.mapdata

import java.io.ByteArrayInputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.zip.GZIPInputStream

/**
 * Reads tiles from a PMTiles v3 archive (the offline region, ADR-0009), through [read] (offset,
 * length) so the phone opens a large file without loading it. Hilbert tile IDs and gzipped
 * directories, as tools/make_chunks.py's reader.
 */
class PmTiles(
    private val read: (offset: Long, length: Int) -> ByteArray,
) {
    private val rootOffset: Long
    private val rootLength: Int
    private val leafOffset: Long
    private val tileOffset: Long
    private val internalCompression: Int
    private val tileCompression: Int
    val minZoom: Int
    val maxZoom: Int

    init {
        val h = ByteBuffer.wrap(read(0, HEADER)).order(ByteOrder.LITTLE_ENDIAN)
        require(String(h.array(), 0, MAGIC.length, Charsets.US_ASCII) == MAGIC && h.get(VERSION_AT).toInt() == 3) {
            "not a PMTiles v3 archive"
        }
        rootOffset = h.getLong(ROOT_AT)
        rootLength = h.getLong(ROOT_AT + LONG).toInt()
        leafOffset = h.getLong(LEAF_AT)
        tileOffset = h.getLong(TILES_AT)
        internalCompression = h.get(COMPRESSION_AT).toInt()
        tileCompression = h.get(COMPRESSION_AT + 1).toInt()
        minZoom = h.get(ZOOM_AT).toInt()
        maxZoom = h.get(ZOOM_AT + 1).toInt()
    }

    private class Entry(
        val tileId: Long,
        val runLength: Long,
        val length: Int,
        val offset: Long,
    )

    /** The tile's bytes (decompressed), or null if the archive doesn't have it. */
    fun tile(
        z: Int,
        x: Int,
        y: Int,
    ): ByteArray? {
        val id = tileId(z, x, y)
        var offset = rootOffset
        var length = rootLength
        repeat(MAX_DEPTH) {
            val found = directory(read(offset, length)).lastOrNull { it.tileId <= id } ?: return null
            if (found.runLength == 0L) { // a leaf directory
                offset = leafOffset + found.offset
                length = found.length
            } else {
                if (id >= found.tileId + found.runLength) return null
                return decompress(read(tileOffset + found.offset, found.length), tileCompression)
            }
        }
        return null
    }

    private fun directory(raw: ByteArray): List<Entry> {
        val d = decompress(raw, internalCompression)
        var i = 0

        fun next(): Long {
            var shift = 0
            var result = 0L
            while (true) {
                val b = d[i++].toInt() and BYTE
                result = result or ((b and SEVEN_BITS).toLong() shl shift)
                if (b and MORE == 0) return result
                shift += 7
            }
        }
        val n = next().toInt()
        val ids = LongArray(n)
        var last = 0L
        for (k in 0 until n) {
            last += next()
            ids[k] = last
        }
        val runs = LongArray(n) { next() }
        val lengths = IntArray(n) { next().toInt() }
        val offsets = LongArray(n)
        for (k in 0 until n) {
            val v = next()
            offsets[k] = if (v == 0L && k > 0) offsets[k - 1] + lengths[k - 1] else v - 1
        }
        return List(n) { Entry(ids[it], runs[it], lengths[it], offsets[it]) }
    }

    private fun decompress(
        data: ByteArray,
        compression: Int,
    ): ByteArray =
        when (compression) {
            NONE -> data
            GZIP -> GZIPInputStream(ByteArrayInputStream(data)).use { it.readBytes() }
            else -> throw IllegalArgumentException("compression $compression")
        }

    companion object {
        private const val MAGIC = "PMTiles"
        private const val HEADER = 127
        private const val VERSION_AT = 7
        private const val ROOT_AT = 8
        private const val LEAF_AT = 40
        private const val TILES_AT = 56
        private const val COMPRESSION_AT = 97
        private const val ZOOM_AT = 100
        private const val LONG = 8
        private const val NONE = 1
        private const val GZIP = 2
        private const val MAX_DEPTH = 4 // root plus up to three levels of leaves
        private const val BYTE = 0xFF
        private const val SEVEN_BITS = 0x7F
        private const val MORE = 0x80

        /** PMTiles' Hilbert tile ID. */
        fun tileId(
            z: Int,
            x: Int,
            y: Int,
        ): Long {
            var acc = 0L
            for (k in 0 until z) acc += 1L shl (2 * k) // tiles in all lower zooms
            var d = 0L
            var tx = x.toLong()
            var ty = y.toLong()
            var s = (1L shl z) shr 1
            while (s > 0) {
                val rx = if (tx and s != 0L) 1L else 0L
                val ry = if (ty and s != 0L) 1L else 0L
                d += s * s * ((3 * rx) xor ry)
                if (ry == 0L) {
                    if (rx == 1L) {
                        tx = s - 1 - tx
                        ty = s - 1 - ty
                    }
                    val t = tx
                    tx = ty
                    ty = t
                }
                s = s shr 1
            }
            return acc + d
        }
    }
}
