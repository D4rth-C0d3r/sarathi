package io.github.d4rthc0d3r.sarathi.maps.google

import android.content.Context
import com.google.android.gms.common.api.ApiException
import com.google.android.gms.common.api.CommonStatusCodes
import com.google.android.gms.tasks.Task
import com.google.android.libraries.places.api.Places
import com.google.android.libraries.places.api.model.AutocompleteSessionToken
import com.google.android.libraries.places.api.model.CircularBounds
import com.google.android.libraries.places.api.model.Place.Field
import com.google.android.libraries.places.api.net.FetchPlaceRequest
import com.google.android.libraries.places.api.net.FindAutocompletePredictionsRequest
import com.google.android.libraries.places.api.net.PlacesClient
import com.google.android.libraries.places.api.net.PlacesStatusCodes
import io.github.d4rthc0d3r.sarathi.core.maps.LatLng
import io.github.d4rthc0d3r.sarathi.core.maps.Place
import io.github.d4rthc0d3r.sarathi.core.maps.PlaceSource
import io.github.d4rthc0d3r.sarathi.core.maps.ProviderException
import io.github.d4rthc0d3r.sarathi.core.maps.SearchBackend
import io.github.d4rthc0d3r.sarathi.core.maps.SearchProvider
import io.github.d4rthc0d3r.sarathi.core.maps.SearchSession
import io.github.d4rthc0d3r.sarathi.core.maps.Suggestion
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException
import com.google.android.gms.maps.model.LatLng as GLatLng

/**
 * Google search through Places API (New) (ADR-0006, ADR-0008). Autocomplete and details share a
 * session token, so the autocomplete requests of a session that ends in details are free. Details
 * asks only for Essentials fields (ID, location, address); the name comes from the suggestion,
 * since the display name is a Pro field.
 */
class GooglePlacesSearch(
    context: Context,
    private val apiKey: String?,
) : SearchProvider {
    override val backend = SearchBackend.GOOGLE

    private val appContext = context.applicationContext
    private val client: PlacesClient by lazy {
        if (!Places.isInitialized()) Places.initializeWithNewPlacesApiEnabled(appContext, apiKey.orEmpty())
        Places.createClient(appContext)
    }
    private var token: Pair<String, AutocompleteSessionToken>? = null

    override fun unavailable(): Unavailable? = if (apiKey.isNullOrEmpty()) Unavailable.NO_KEY else null

    override suspend fun autocomplete(
        query: String,
        near: LatLng?,
        session: SearchSession,
    ): List<Suggestion> {
        val request =
            FindAutocompletePredictionsRequest.builder().run {
                setQuery(query)
                setSessionToken(tokenFor(session))
                near?.let {
                    setLocationBias(CircularBounds.newInstance(it.google(), BIAS_RADIUS_M))
                    setOrigin(it.google())
                }
                build()
            }
        return client.findAutocompletePredictions(request).await().autocompletePredictions.map {
            Suggestion(it.getPrimaryText(null).toString(), it.getSecondaryText(null).toString(), backend, it.placeId)
        }
    }

    override suspend fun details(
        suggestion: Suggestion,
        session: SearchSession,
    ): Place {
        val request =
            FetchPlaceRequest
                .builder(suggestion.id, listOf(Field.ID, Field.LOCATION, Field.FORMATTED_ADDRESS))
                .setSessionToken(tokenFor(session))
                .build()
        val place = client.fetchPlace(request).await().place
        token = null // a details call ends the session
        val at = place.location ?: throw ProviderException(Unavailable.ERROR, "no location")
        return Place(
            name = suggestion.title,
            address = place.formattedAddress ?: suggestion.subtitle,
            at = LatLng(at.latitude, at.longitude),
            source = PlaceSource.GOOGLE,
            id = place.id ?: suggestion.id,
        )
    }

    private fun tokenFor(session: SearchSession): AutocompleteSessionToken =
        token?.takeIf { it.first == session.token }?.second
            ?: AutocompleteSessionToken.newInstance().also { token = session.token to it }

    private fun LatLng.google() = GLatLng(lat, lng)

    companion object {
        private const val BIAS_RADIUS_M = 50_000.0
    }
}

/** A Play services task as a suspend call; Places' errors as the fallback chain's reasons. */
private suspend fun <T> Task<T>.await(): T =
    suspendCancellableCoroutine { cont ->
        addOnCompleteListener { task ->
            val e = task.exception
            when {
                e == null && !task.isCanceled -> {
                    cont.resume(task.result)
                }

                e is ApiException -> {
                    cont.resumeWithException(ProviderException(reason(e.statusCode), e.message ?: "", e))
                }

                else -> {
                    cont.resumeWithException(ProviderException(Unavailable.ERROR, e?.message ?: "cancelled", e))
                }
            }
        }
    }

private fun reason(status: Int): Unavailable =
    when (status) {
        PlacesStatusCodes.OVER_QUERY_LIMIT -> Unavailable.OVER_QUOTA
        PlacesStatusCodes.REQUEST_DENIED -> Unavailable.ACCOUNT
        CommonStatusCodes.NETWORK_ERROR -> Unavailable.OFFLINE
        CommonStatusCodes.TIMEOUT -> Unavailable.TIMEOUT
        else -> Unavailable.ERROR
    }
