package io.github.d4rthc0d3r.sarathi.maps.osm

import android.content.Context
import com.valhalla.valhalla.Valhalla
import com.valhalla.valhalla.config.ValhallaConfigFactory
import io.github.d4rthc0d3r.sarathi.core.maps.ProviderException
import io.github.d4rthc0d3r.sarathi.core.maps.Route
import io.github.d4rthc0d3r.sarathi.core.maps.RouteRequest
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingProvider
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runInterruptible
import java.io.File

/**
 * OSM offline routing (ADR-0006): Valhalla on the phone (valhalla-mobile), over tiles built by
 * `tools/build_offline_tiles.sh` and packaged as assets. Always last in the chain. The same
 * request and answer as the online Valhalla, without a network. Tiles from another Valhalla
 * version are refused.
 */
class OfflineRouting(
    context: Context,
) : RoutingProvider {
    override val backend = RoutingBackend.OSM_OFFLINE

    private val appContext = context.applicationContext
    private val stamp: String? by lazy { readStamp() }
    private val valhalla: Valhalla by lazy {
        Valhalla(appContext, ValhallaConfigFactory.usingTileExtract(tilesFile().absolutePath))
    }

    override fun unavailable(): Unavailable? = if (stamp == null) Unavailable.NO_DATA else null

    override suspend fun route(request: RouteRequest): Route {
        val body =
            runInterruptible(Dispatchers.IO) {
                try {
                    valhalla.routeRaw(ValhallaRouting.body(request).toString())
                } catch (
                    @Suppress("TooGenericExceptionCaught") e: Exception, // the native router's errors
                ) {
                    throw ValhallaRouting.failure(e.message.orEmpty(), "Valhalla offline", e)
                }
            }
        if (ValhallaRouting.errorCode(body) != null) throw ValhallaRouting.failure(body, "Valhalla offline")
        return ValhallaRouting.parse(body, request.mode, backend, ATTRIBUTION)
    }

    /** The packaged tiles' stamp, if they're there and built for this Valhalla. */
    private fun readStamp(): String? =
        runCatching {
            appContext.assets
                .open("$ASSET_DIR/tiles.version")
                .bufferedReader()
                .readText()
        }.getOrNull()?.takeIf { it.lines().contains("valhalla=$VALHALLA_VERSION") }

    /** Valhalla reads a file, not an asset: copied out once per build of the tiles. */
    private fun tilesFile(): File {
        val dir = File(appContext.filesDir, ASSET_DIR).apply { mkdirs() }
        val file = File(dir, "tiles.tar")
        val stampFile = File(dir, "tiles.version")
        if (!file.exists() || stampFile.takeIf { it.exists() }?.readText() != stamp) {
            appContext.assets.open("$ASSET_DIR/tiles.tar").use { input -> file.outputStream().use { input.copyTo(it) } }
            stampFile.writeText(stamp.orEmpty())
        }
        return file
    }

    companion object {
        /** valhalla-mobile 0.6.4's Valhalla; the tiles must be built with the same. */
        const val VALHALLA_VERSION = "3.9.1"
        const val ATTRIBUTION = "© OpenStreetMap contributors · Valhalla (offline)"
        private const val ASSET_DIR = "valhalla"
    }
}
