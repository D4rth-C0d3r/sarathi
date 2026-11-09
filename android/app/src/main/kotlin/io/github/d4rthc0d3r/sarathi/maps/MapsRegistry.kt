package io.github.d4rthc0d3r.sarathi.maps

import android.app.Application
import android.content.Context
import android.util.Log
import io.github.d4rthc0d3r.sarathi.BuildConfig
import io.github.d4rthc0d3r.sarathi.core.mapdata.MapData
import io.github.d4rthc0d3r.sarathi.core.mapdata.TileCache
import io.github.d4rthc0d3r.sarathi.core.maps.FailureDrill
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingProvider
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingService
import io.github.d4rthc0d3r.sarathi.core.maps.SearchBackend
import io.github.d4rthc0d3r.sarathi.core.maps.SearchProvider
import io.github.d4rthc0d3r.sarathi.core.maps.SearchService
import io.github.d4rthc0d3r.sarathi.maps.google.GoogleNavRouting
import io.github.d4rthc0d3r.sarathi.maps.google.GooglePlacesSearch
import io.github.d4rthc0d3r.sarathi.maps.mappls.MapplsRouting
import io.github.d4rthc0d3r.sarathi.maps.mappls.MapplsSearch
import io.github.d4rthc0d3r.sarathi.maps.osm.GraphHopperRouting
import io.github.d4rthc0d3r.sarathi.maps.osm.OfflineMapRegion
import io.github.d4rthc0d3r.sarathi.maps.osm.OfflineRouting
import io.github.d4rthc0d3r.sarathi.maps.osm.OpenFreeMapTiles
import io.github.d4rthc0d3r.sarathi.maps.osm.PhotonSearch
import io.github.d4rthc0d3r.sarathi.maps.osm.ValhallaRouting
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runInterruptible
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.File

/** The maps backends this build has (ADR-0006), and search across them. One per process. */
class MapsRegistry private constructor(
    context: Context,
) {
    private val store = MapsStore.get(context)
    private val http = OkHttpClient()

    /** Identifies us to the free OSM services, as their terms ask. */
    val userAgent = "sarathi/${BuildConfig.VERSION_NAME} (+https://github.com/D4rth-C0d3r/sarathi)"

    private val searchProviders: Map<SearchBackend, SearchProvider> =
        mapOf(
            SearchBackend.GOOGLE to GooglePlacesSearch(context, Keys.google),
            SearchBackend.MAPPLS to MapplsSearch(context, Keys.mapplsConfigured),
            SearchBackend.OSM to PhotonSearch(http, userAgent),
        )

    val search = SearchService(searchProviders::get, { store.settings.value }, { store.guard })

    /** Google's routing, which its guidance continues on (step F6.3). */
    val googleRouting = GoogleNavRouting(context as Application, Keys.google)

    private val routingProviders: Map<RoutingBackend, RoutingProvider> =
        mapOf(
            RoutingBackend.GOOGLE to googleRouting,
            RoutingBackend.MAPPLS to MapplsRouting(context, Keys.mapplsConfigured),
            RoutingBackend.OSM_VALHALLA to ValhallaRouting(http, userAgent, { store.settings.value.valhallaUrl }),
            RoutingBackend.OSM_GRAPHHOPPER to GraphHopperRouting(http, userAgent, Keys.graphHopper),
            RoutingBackend.OSM_OFFLINE to OfflineRouting(context),
        )

    /** Desk tests: the fallback chain made to fail on purpose (step F8, debug builds). */
    val drill = FailureDrill()

    val routing = RoutingService(routingProviders::get, { store.settings.value }, { store.guard }, drill)

    /** Map tiles for the dash (ADR-0009): OpenFreeMap online, the offline region, a 200 MB cache. */
    val onlineMap = OpenFreeMapTiles(http, userAgent)
    val offlineMap = OfflineMapRegion(context)
    val mapCache = TileCache(File(context.filesDir, "map-cache"))
    val mapData = MapData(mapCache, onlineMap, offlineMap) { Log.i(MAP_TAG, it) }

    /**
     * The desk check's "no signal": no online source, and a cache of its own (cleared each time),
     * so the tiles really come from the offline region.
     */
    fun offlineMapData(context: Context): MapData {
        val dir = File(context.cacheDir, "map-offline-check").apply { deleteRecursively() }
        return MapData(TileCache(dir), null, offlineMap) { Log.i(MAP_TAG, it) }
    }

    private val noRedirects =
        http
            .newBuilder()
            .followRedirects(false)
            .followSslRedirects(false)
            .build()

    /**
     * One hop of a short link (ADR-0008): its `Location` header, without following it or reading
     * the page. HEAD first; a GET whose body is never read if HEAD gives no redirect.
     */
    suspend fun redirect(url: String): String? =
        runInterruptible(Dispatchers.IO) {
            listOf("HEAD", "GET").firstNotNullOfOrNull { method ->
                val request =
                    Request
                        .Builder()
                        .url(url)
                        .method(method, null)
                        .header("User-Agent", userAgent)
                        .build()
                runCatching {
                    noRedirects.newCall(request).execute().use {
                        it
                            .takeIf { r ->
                                r.isRedirect
                            }?.header("Location")
                    }
                }.getOrNull()
            }
        }

    companion object {
        private const val MAP_TAG = "Map"

        @Volatile private var instance: MapsRegistry? = null

        fun get(context: Context): MapsRegistry =
            instance
                ?: synchronized(this) { instance ?: MapsRegistry(context.applicationContext).also { instance = it } }
    }
}
