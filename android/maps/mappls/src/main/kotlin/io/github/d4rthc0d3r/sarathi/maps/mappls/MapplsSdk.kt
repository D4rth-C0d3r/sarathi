package io.github.d4rthc0d3r.sarathi.maps.mappls

import android.content.Context
import com.mappls.android.lms.MapplsLMSAPI
import com.mappls.sdk.core.MapplsInitialiser
import com.mappls.sdk.services.api.ApiResponse
import io.github.d4rthc0d3r.sarathi.core.maps.ProviderException
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runInterruptible
import java.io.IOException

/** Mappls's SDK, started once with its analytics opted out, and its calls as the chain's errors. */
internal object MapplsSdk {
    /** The token MapplsLMSManager.initialize(Context) uses for the native Android SDK. */
    private const val LMS_TOKEN = "mappls-android-sdk"
    private const val HTTP_OK = 200
    private const val HTTP_NO_CONTENT = 204
    private const val HTTP_UNAUTHORIZED = 401
    private const val HTTP_FORBIDDEN = 403
    private const val HTTP_TOO_MANY = 429

    @Volatile private var started = false

    private fun start(context: Context) {
        if (started) return
        synchronized(this) {
            if (started) return
            // Mappls's core starts a usage-analytics client (device model, OS, app name and
            // version) under this token. Opting out first, through its public API, keeps it from
            // sending anything; the opt-out is stored on the phone.
            MapplsLMSAPI.getInstance(context, LMS_TOKEN, false).optOutTracking()
            MapplsInitialiser.getInstance().initialise(context)
            started = true
        }
    }

    /** Runs a blocking SDK call off the main thread; null for "nothing found" (HTTP 204). */
    suspend fun <T> call(
        context: Context,
        execute: () -> ApiResponse<T>,
    ): T? {
        val response =
            try {
                runInterruptible(Dispatchers.IO) {
                    start(context.applicationContext)
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
}
