package io.github.d4rthc0d3r.sarathi.core.mapdata

import io.github.d4rthc0d3r.sarathi.core.protocol.Provenance
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.File
import java.io.IOException
import java.util.zip.GZIPInputStream
import java.util.zip.GZIPOutputStream

/** Which processed tile: the tile, the source's version and its provenance (ADR-0009 "Phone cache"). */
data class TileKey(
    val tile: TileId,
    val dataVersion: Long,
    val provenance: Provenance,
)

/**
 * Processed tiles ([TileShapes], ride-independent) on the phone's storage, capped at [capBytes]:
 * the least recently used go first (ADR-0009, default 200 MB). One gzipped file per tile
 * ([TileFile]), so a cached tile makes exactly the chunk a fresh one does. One caller at a time.
 */
class TileCache(
    private val dir: File,
    private val capBytes: Long = DEFAULT_CAP,
    private val now: () -> Long = System::currentTimeMillis,
) {
    private var total = -1L // bytes on disk; counted on first use

    fun get(key: TileKey): TileShapes? = load(fileOf(key))

    /** The newest cached version of a tile from any source, for stretches without signal. */
    fun newest(tile: TileId): TileShapes? {
        val prefix = "${tile.x}-${tile.y}-"
        val candidates = File(dir, "L${tile.level.code}").listFiles { f -> f.name.startsWith(prefix) }.orEmpty()
        val best =
            candidates.maxByOrNull {
                it.name
                    .removePrefix(prefix)
                    .substringBefore('-')
                    .toLongOrNull() ?: 0
            }
        return best?.let { load(it) }
    }

    fun put(shapes: TileShapes) {
        val file = fileOf(TileKey(shapes.tile, shapes.dataVersion, shapes.provenance))
        file.parentFile.mkdirs()
        val tmp = File(file.path + ".tmp")
        TileFile.write(tmp, shapes)
        val before = if (file.exists()) file.length() else 0
        if (!tmp.renameTo(file)) throw IOException("could not store $file")
        file.setLastModified(now())
        total = if (total < 0) size() else total + file.length() - before
        if (total > capBytes) trim()
    }

    /** Bytes on disk now. */
    fun size(): Long = files().sumOf { it.length() }

    private fun load(file: File): TileShapes? {
        if (!file.exists()) return null
        return try {
            TileFile.read(file).also { file.setLastModified(now()) }
        } catch (_: IOException) {
            file.delete() // damaged: fetched again next time
            total = -1
            null
        }
    }

    private fun trim() {
        val sorted = files().sortedBy { it.lastModified() }
        var size = sorted.sumOf { it.length() }
        val target = capBytes * TRIM_TO_PERCENT / PERCENT // a little room, so it doesn't trim on every put
        for (f in sorted) {
            if (size <= target) break
            size -= f.length()
            f.delete()
        }
        total = size
    }

    private fun files(): List<File> = dir.walkTopDown().filter { it.isFile && it.name.endsWith(TileFile.EXT) }.toList()

    private fun fileOf(key: TileKey): File {
        val t = key.tile
        return File(dir, "L${t.level.code}/${t.x}-${t.y}-${key.dataVersion}-${key.provenance.code}${TileFile.EXT}")
    }

    companion object {
        const val DEFAULT_CAP = 200L * 1024 * 1024
        private const val TRIM_TO_PERCENT = 90
        private const val PERCENT = 100
    }
}

/** One cached tile on disk: gzipped, points as doubles in tile units. */
internal object TileFile {
    const val EXT = ".shp"
    private const val MAGIC = 0x53484131 // "SHA1": shapes, format 1

    fun write(
        file: File,
        s: TileShapes,
    ) = DataOutputStream(GZIPOutputStream(file.outputStream().buffered())).use { out ->
        out.writeInt(MAGIC)
        out.writeByte(s.level.code)
        out.writeInt(s.tileX)
        out.writeInt(s.tileY)
        out.writeLong(s.dataVersion)
        out.writeByte(s.provenance.code)
        writeShapes(out, s.lines)
        writeShapes(out, s.polys)
    }

    private fun writeShapes(
        out: DataOutputStream,
        list: List<TileShape>,
    ) {
        out.writeInt(list.size)
        for (shape in list) {
            out.writeByte(shape.kind)
            out.writeInt(shape.points.size)
            for (p in shape.points) {
                out.writeDouble(p.x)
                out.writeDouble(p.y)
            }
        }
    }

    fun read(file: File): TileShapes =
        DataInputStream(GZIPInputStream(file.inputStream().buffered())).use { input ->
            if (input.readInt() != MAGIC) throw IOException("not a cached tile")
            val levelCode = input.readByte().toInt()
            val level = MapLevel.entries.firstOrNull { it.code == levelCode } ?: throw IOException("level $levelCode")
            val tile = TileId(level, input.readInt(), input.readInt())
            val version = input.readLong()
            val provCode = input.readByte().toInt()
            val provenance =
                Provenance.entries.firstOrNull { it.code == provCode } ?: throw IOException("provenance $provCode")
            val lists =
                List(2) {
                    List(input.readInt()) {
                        val kind = input.readByte().toInt()
                        TileShape(kind, List(input.readInt()) { TilePoint(input.readDouble(), input.readDouble()) })
                    }
                }
            TileShapes(tile, lists[0], lists[1], provenance, version)
        }
}
