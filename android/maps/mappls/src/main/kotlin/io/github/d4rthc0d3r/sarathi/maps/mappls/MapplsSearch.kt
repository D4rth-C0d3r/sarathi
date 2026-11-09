package io.github.d4rthc0d3r.sarathi.maps.mappls

import android.content.Context
import com.mappls.android.lms.MapplsLMSAPI
import com.mappls.sdk.core.MapplsInitialiser
import com.mappls.sdk.services.api.ApiResponse
import com.mappls.sdk.services.api.autosuggest.MapplsAutoSuggest
import com.mappls.sdk.services.api.autosuggest.MapplsAutosuggestManager
import com.mappls.sdk.services.api.placedetail.MapplsPlaceDetail
import com.mappls.sdk.services.api.placedetail.MapplsPlaceDetailManager
import com.mappls.sdk.services.api.reversegeocode.MapplsReverseGeoCode
import com.mappls.sdk.services.api.reversegeocode.MapplsReverseGeoCodeManager
import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.maps.Place
import io.github.d4rthc0d3r.sarathi.core.maps.PlaceSource
import io.github.d4rthc0d3r.sarathi.core.maps.ProviderException
import io.github.d4rthc0d3r.sarathi.core.maps.SearchBackend
import io.github.d4rthc0d3r.sarathi.core.maps.SearchProvider
import io.github.d4rthc0d3r.sarathi.core.maps.SearchSession
import io.github.d4rthc0d3r.sarathi.core.maps.Suggestion
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runInterruptible
import java.io.IOException

/**
 * Mappls search (ADR-0006): Autosuggest, which also takes a Mappls Pin typed directly, and place
 * details by Mappls Pin for the coordinates. [configured] is false in a build without Mappls's
 * config files.
 */
class MapplsSearch(
    context: Context,
    private val configured: Boolean,
) : SearchProvider {
    override val backend = SearchBackend.MAPPLS

    private val appContext = context.applicationContext
    private val ready by lazy {
        // Mappls's core starts a usage-analytics client (device model, OS, app name and version)
        // under this token. Opting out first, through its public API, keeps it from sending
        // anything; the opt-out is stored on the phone.
        MapplsLMSAPI.getInstance(appContext, LMS_TOKEN, false).optOutTracking()
        MapplsInitialiser.getInstance().initialise(appContext)
    }

    override fun unavailable(): Unavailable? = if (configured) null else Unavailable.NO_KEY

    override suspend fun autocomplete(
        query: String,
        near: LatLng?,
        session: SearchSession,
    ): List<Suggestion> {
        val request =
            MapplsAutoSuggest.builder().run {
                query(query)
                near?.let { setLocation(it.lat, it.lng) }
                build()
            }
        val response = call { MapplsAutosuggestManager.newInstance(request).executeCall() } ?: return emptyList()
        return response.suggestedLocations.orEmpty().mapNotNull { e ->
            val pin = e.mapplsPin ?: return@mapNotNull null
            val at = e.latitude?.let { lat -> e.longitude?.let { LatLng(lat, it) } }
            Suggestion(e.placeName ?: pin, e.placeAddress.orEmpty(), backend, pin, at)
        }
    }

    override suspend fun details(
        suggestion: Suggestion,
        session: SearchSession,
    ): Place = byPin(suggestion) ?: placeDetail(suggestion)

    /**
     * Autosuggest given the Pin itself answers with its coordinates; place details by Pin answered
     * "no content" on the free plan (checked on the phone, 2026-11-09), so it's only the fallback.
     */
    private suspend fun byPin(suggestion: Suggestion): Place? {
        val request = MapplsAutoSuggest.builder().query(suggestion.id).build()
        val e =
            call { MapplsAutosuggestManager.newInstance(request).executeCall() }
                ?.suggestedLocations
                ?.firstOrNull { it.mapplsPin.equals(suggestion.id, ignoreCase = true) }
        val lat = e?.latitude
        val lng = e?.longitude
        return if (lat == null || lng == null) {
            null
        } else {
            Place(
                e.placeName ?: suggestion.title,
                e.placeAddress ?: suggestion.subtitle,
                LatLng(lat, lng),
                PlaceSource.MAPPLS,
                e.mapplsPin,
            )
        }
    }

    private suspend fun placeDetail(suggestion: Suggestion): Place {
        val request = MapplsPlaceDetail.builder().mapplsPin(suggestion.id).build()
        val d =
            call { MapplsPlaceDetailManager.newInstance(request).executeCall() }
                ?: throw ProviderException(Unavailable.ERROR, "no place for ${suggestion.id}")
        val lat = d.latitude
        val lng = d.longitude
        if (lat == null || lng == null) throw ProviderException(Unavailable.ERROR, "no coordinates")
        return Place(
            name = d.placeName ?: suggestion.title,
            address = d.address ?: suggestion.subtitle,
            at = LatLng(lat, lng),
            source = PlaceSource.MAPPLS,
            id = suggestion.id,
        )
    }

    override suspend fun reverseGeocode(at: LatLng): Place? {
        val request = MapplsReverseGeoCode.builder().setLocation(at.lat, at.lng).build()
        val places = call { MapplsReverseGeoCodeManager.newInstance(request).executeCall() }?.places
        val p = places?.firstOrNull() ?: return null
        val address = p.formattedAddress.orEmpty()
        // Mappls leaves fields empty rather than null: the first one with text names the point.
        val name =
            listOf(p.poi, p.street, p.subLocality, p.locality, p.village, p.city, address).firstOrNull {
                !it.isNullOrBlank()
            }
        return name?.let { Place(it, address, at, PlaceSource.MAPPLS, p.mapplsPin) }
    }

    /** Runs a blocking SDK call off the main thread; null for "nothing found" (HTTP 204). */
    private suspend fun <T> call(execute: () -> ApiResponse<T>): T? {
        val response =
            try {
                runInterruptible(Dispatchers.IO) {
                    ready
                    execute()
                }
            } catch (e: IOException) {
                throw ProviderException(Unavailable.OFFLINE, e.message ?: "network", e)
            }
        return when (response.responseCode) {
            HTTP_OK -> response.response
            HTTP_NO_CONTENT -> null
            else -> throw failure(response.responseCode, response.errorMessage)
        }
    }

    private fun failure(
        code: Int,
        message: String?,
    ): ProviderException {
        val reason =
            when (code) {
                HTTP_UNAUTHORIZED -> Unavailable.ACCOUNT

                // "Services for this key has been suspended due to daily/hourly transactions limit"
                HTTP_FORBIDDEN, HTTP_TOO_MANY -> Unavailable.OVER_QUOTA

                else -> Unavailable.ERROR
            }
        return ProviderException(reason, "Mappls $code: $message")
    }

    private companion object {
        /** The token MapplsLMSManager.initialize(Context) uses for the native Android SDK. */
        const val LMS_TOKEN = "mappls-android-sdk"
        const val HTTP_OK = 200
        const val HTTP_NO_CONTENT = 204
        const val HTTP_UNAUTHORIZED = 401
        const val HTTP_FORBIDDEN = 403
        const val HTTP_TOO_MANY = 429
    }
}
