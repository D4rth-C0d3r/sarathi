package io.github.d4rthc0d3r.sarathi.maps.osm

import io.github.d4rthc0d3r.sarathi.core.mapdata.MapSchema
import io.github.d4rthc0d3r.sarathi.core.mapdata.TileSource
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runInterruptible
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.ByteArrayInputStream
import java.io.IOException
import java.util.zip.GZIPInputStream

/**
 * OpenFreeMap's vector tiles (ADR-0009): no key and no request limits. The TileJSON names the
 * current planet build; its date is the data version every chunk carries. Rechecked hourly.
 */
class OpenFreeMapTiles(
    private val http: OkHttpClient,
    private val userAgent: String,
    private val tileJsonUrl: String = TILEJSON,
    private val now: () -> Long = System::currentTimeMillis,
) : TileSource {
    override val schema = MapSchema.OpenMapTiles

    private val lock = Mutex()
    private var template: String? = null
    private var version: Long? = null
    private var checkedAt = 0L

    override suspend fun dataVersion(): Long? =
        lock.withLock {
            if (template == null || now() - checkedAt > RECHECK_MS) {
                val (t, v) = parseTileJson(String(get(tileJsonUrl) ?: throw IOException("no TileJSON")))
                template = t
                version = v
                checkedAt = now()
            }
            version
        }

    override suspend fun tile(
        z: Int,
        x: Int,
        y: Int,
    ): ByteArray? {
        dataVersion()
        val url =
            lock
                .withLock { template }!!
                .replace("{z}", z.toString())
                .replace("{x}", x.toString())
                .replace("{y}", y.toString())
        return get(url)?.let(::gunzipIfNeeded)
    }

    private suspend fun get(url: String): ByteArray? =
        runInterruptible(Dispatchers.IO) {
            val request =
                Request
                    .Builder()
                    .url(url)
                    .header("User-Agent", userAgent)
                    .build()
            http.newCall(request).execute().use { r ->
                when {
                    r.isSuccessful -> r.body.bytes()

                    r.code == HTTP_NOT_FOUND || r.code == HTTP_NO_CONTENT -> null

                    // no tile there
                    else -> throw IOException("HTTP ${r.code} for $url")
                }
            }
        }

    companion object {
        const val TILEJSON = "https://tiles.openfreemap.org/planet"
        const val ATTRIBUTION = "© OpenMapTiles, data from OpenStreetMap"
        private const val RECHECK_MS = 60 * 60 * 1000L
        private const val HTTP_NOT_FOUND = 404
        private const val HTTP_NO_CONTENT = 204
        private const val VERSION_DIGITS = 8

        /** The tile URL template and the build date from a TileJSON. */
        fun parseTileJson(json: String): Pair<String, Long> {
            val template =
                Json
                    .parseToJsonElement(json)
                    .jsonObject["tiles"]
                    ?.jsonArray
                    ?.firstOrNull()
                    ?.jsonPrimitive
                    ?.content ?: throw IOException("TileJSON without tiles")
            // .../planet/20261101_080013_pt/{z}/{x}/{y}.pbf
            val build =
                Regex("/(\\d{$VERSION_DIGITS})_")
                    .find(template)
                    ?.groupValues
                    ?.get(1)
                    ?.toLong()
                    ?: throw IOException("no build date in $template")
            return template to build
        }

        /** Tiles may come gzipped without a Content-Encoding header. */
        fun gunzipIfNeeded(bytes: ByteArray): ByteArray =
            if (bytes.size > 2 && bytes[0] == GZIP_0 && bytes[1] == GZIP_1) {
                GZIPInputStream(ByteArrayInputStream(bytes)).use { it.readBytes() }
            } else {
                bytes
            }

        private const val GZIP_0 = 0x1f.toByte()
        private const val GZIP_1 = 0x8b.toByte()
    }
}
