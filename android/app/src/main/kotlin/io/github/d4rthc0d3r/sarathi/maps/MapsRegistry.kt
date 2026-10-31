package io.github.d4rthc0d3r.sarathi.maps

import android.content.Context
import io.github.d4rthc0d3r.sarathi.BuildConfig
import io.github.d4rthc0d3r.sarathi.core.maps.SearchBackend
import io.github.d4rthc0d3r.sarathi.core.maps.SearchProvider
import io.github.d4rthc0d3r.sarathi.core.maps.SearchService
import io.github.d4rthc0d3r.sarathi.maps.google.GooglePlacesSearch
import io.github.d4rthc0d3r.sarathi.maps.mappls.MapplsSearch
import io.github.d4rthc0d3r.sarathi.maps.osm.PhotonSearch
import okhttp3.OkHttpClient

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

    companion object {
        @Volatile private var instance: MapsRegistry? = null

        fun get(context: Context): MapsRegistry =
            instance
                ?: synchronized(this) { instance ?: MapsRegistry(context.applicationContext).also { instance = it } }
    }
}
