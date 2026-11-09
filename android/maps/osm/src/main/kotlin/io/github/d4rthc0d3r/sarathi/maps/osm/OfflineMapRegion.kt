package io.github.d4rthc0d3r.sarathi.maps.osm

import android.content.Context
import io.github.d4rthc0d3r.sarathi.core.mapdata.MapSchema
import io.github.d4rthc0d3r.sarathi.core.mapdata.PmTiles
import io.github.d4rthc0d3r.sarathi.core.mapdata.TileSource
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileNotFoundException
import java.io.RandomAccessFile

/**
 * The offline map region (ADR-0009): a Protomaps extract built by tools/build_offline_map.sh and
 * packaged as assets. Copied once to the app's storage, then read in place through PMTiles.
 * Without the assets, [dataVersion] is null and the map is online only.
 */
class OfflineMapRegion(
    context: Context,
) : TileSource {
    override val schema = MapSchema.Protomaps

    private val app = context.applicationContext
    private var region: PmTiles? = null
    private var file: RandomAccessFile? = null
    private var version: Long? = null
    private var opened = false

    override suspend fun dataVersion(): Long? = withContext(Dispatchers.IO) { open() }

    override suspend fun tile(
        z: Int,
        x: Int,
        y: Int,
    ): ByteArray? =
        withContext(Dispatchers.IO) {
            open()
            region?.tile(z, x, y)
        }

    @Synchronized
    private fun open(): Long? {
        if (!opened) {
            opened = true
            version = readBuild()?.also { openRegion(it) }
        }
        return version
    }

    /** The region's build date from its version file; null without a region in this build. */
    private fun readBuild(): Long? =
        try {
            app.assets
                .open("$ASSET_DIR/region.version")
                .bufferedReader()
                .readLines()
                .firstOrNull { it.startsWith("build=") }
                ?.substringAfter('=')
                ?.toLongOrNull()
        } catch (_: FileNotFoundException) {
            null
        }

    private fun openRegion(build: Long) {
        val dir = File(app.filesDir, "offline-map").apply { mkdirs() }
        val target = File(dir, "region-$build.pmtiles")
        if (!target.exists()) {
            dir.listFiles()?.forEach { it.delete() } // an older build
            val tmp = File(dir, "region.tmp")
            app.assets.open("$ASSET_DIR/region.pmtiles").use { input -> tmp.outputStream().use { input.copyTo(it) } }
            tmp.renameTo(target)
        }
        val raf = RandomAccessFile(target, "r")
        file = raf
        region =
            PmTiles { offset, length ->
                synchronized(raf) {
                    ByteArray(length).also {
                        raf.seek(offset)
                        raf.readFully(it)
                    }
                }
            }
    }

    private companion object {
        const val ASSET_DIR = "map"
    }
}
