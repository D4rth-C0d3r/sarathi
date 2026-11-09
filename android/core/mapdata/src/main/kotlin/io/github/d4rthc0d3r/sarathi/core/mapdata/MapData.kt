package io.github.d4rthc0d3r.sarathi.core.mapdata

import java.io.IOException

/** Where vector tiles come from: OpenFreeMap online, or the offline region (ADR-0009). */
interface TileSource {
    val schema: MapSchema

    /** The data's version (the build date), or null if the source can't be reached now. */
    suspend fun dataVersion(): Long?

    /** A tile's bytes (decompressed), or null if the source has no such tile. */
    @Throws(IOException::class)
    suspend fun tile(
        z: Int,
        x: Int,
        y: Int,
    ): ByteArray?
}

/** Where a tile came from, for logs and the desk check. */
enum class TileOrigin { CACHE, ONLINE, CACHE_OLDER, OFFLINE }

/**
 * Processed tiles for the map, in this order (ADR-0009):
 * 1. the cache, for the online source's current version
 * 2. the online source (OpenFreeMap), cached
 * 3. any cached version from any source: a corridor prefetched before the ride
 * 4. the offline region, cached
 *
 * Google or Mappls data never comes here: only OSM-derived tiles become chunks.
 */
class MapData(
    private val cache: TileCache,
    private val online: TileSource?,
    private val offline: TileSource?,
    private val log: (String) -> Unit = {},
) {
    class Result(
        val shapes: TileShapes,
        val origin: TileOrigin,
    )

    suspend fun shapes(tile: TileId): Result? =
        from(online, tile, TileOrigin.ONLINE)
            ?: cache.newest(tile)?.let { Result(it, TileOrigin.CACHE_OLDER) }
            ?: from(offline, tile, TileOrigin.OFFLINE)

    /** The source's current version from the cache, else fetched and cached. */
    private suspend fun from(
        source: TileSource?,
        tile: TileId,
        fresh: TileOrigin,
    ): Result? =
        source?.let { src ->
            safeVersion(src)?.let { version ->
                cache.get(TileKey(tile, version, src.schema.provenance))?.let { Result(it, TileOrigin.CACHE) }
                    ?: fetch(src, version, tile)?.let { Result(it, fresh) }
            }
        }

    private suspend fun safeVersion(source: TileSource): Long? =
        try {
            source.dataVersion()
        } catch (e: IOException) {
            log("map: ${source.schema} unreachable: ${e.message}")
            null
        }

    private suspend fun fetch(
        source: TileSource,
        version: Long,
        tile: TileId,
    ): TileShapes? =
        try {
            source.tile(tile.level.zoom, tile.x, tile.y)?.let { bytes ->
                val features = VectorTile.read(bytes, source.schema.layers)
                ChunkMaker.shapes(features, tile, source.schema, version).also { cache.put(it) }
            }
        } catch (e: IOException) {
            log("map: ${source.schema} tile ${tile.level.zoom}/${tile.x}/${tile.y} failed: ${e.message}")
            null
        }
}
