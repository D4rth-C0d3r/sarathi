package io.github.d4rthc0d3r.sarathi.maps.osm

import io.github.d4rthc0d3r.sarathi.core.maps.ProviderException
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable
import kotlinx.coroutines.suspendCancellableCoroutine
import okhttp3.Call
import okhttp3.Callback
import okhttp3.HttpUrl
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import java.io.IOException
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

/** GETs [url] and returns the body; failures as [ProviderException]s the fallback chain understands. */
internal suspend fun OkHttpClient.getText(
    url: HttpUrl,
    userAgent: String,
): String {
    val request =
        Request
            .Builder()
            .url(url)
            .header("User-Agent", userAgent)
            .build()
    val response =
        suspendCancellableCoroutine { cont ->
            val call = newCall(request)
            cont.invokeOnCancellation { call.cancel() }
            call.enqueue(
                object : Callback {
                    override fun onFailure(
                        call: Call,
                        e: IOException,
                    ) = cont.resumeWithException(ProviderException(Unavailable.OFFLINE, e.message ?: "network", e))

                    override fun onResponse(
                        call: Call,
                        response: Response,
                    ) = cont.resume(response)
                },
            )
        }
    return response.use {
        when {
            it.isSuccessful -> it.body.string()
            it.code == HTTP_TOO_MANY -> throw ProviderException(Unavailable.OVER_QUOTA, "HTTP ${it.code}")
            else -> throw ProviderException(Unavailable.ERROR, "HTTP ${it.code}")
        }
    }
}

private const val HTTP_TOO_MANY = 429
