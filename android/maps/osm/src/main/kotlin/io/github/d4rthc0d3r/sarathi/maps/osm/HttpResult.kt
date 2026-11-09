package io.github.d4rthc0d3r.sarathi.maps.osm

import io.github.d4rthc0d3r.sarathi.core.maps.ProviderException
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable
import kotlinx.coroutines.suspendCancellableCoroutine
import okhttp3.Call
import okhttp3.Callback
import okhttp3.HttpUrl
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.Response
import java.io.IOException
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

internal class HttpResult(
    val code: Int,
    val body: String,
) {
    val ok get() = code in HTTP_OK_RANGE
}

/** GETs [url] and returns the body; failures as [ProviderException]s the fallback chain understands. */
internal suspend fun OkHttpClient.getText(
    url: HttpUrl,
    userAgent: String,
): String {
    val result =
        send(
            Request
                .Builder()
                .url(url)
                .header("User-Agent", userAgent)
                .build(),
        )
    return when {
        result.ok -> result.body
        result.code == HTTP_TOO_MANY -> throw ProviderException(Unavailable.OVER_QUOTA, "HTTP ${result.code}")
        else -> throw ProviderException(Unavailable.ERROR, "HTTP ${result.code}")
    }
}

/** POSTs JSON; the body comes back whatever the status, for the backend's own error codes. */
internal suspend fun OkHttpClient.postJson(
    url: HttpUrl,
    userAgent: String,
    json: String,
): HttpResult =
    send(
        Request
            .Builder()
            .url(url)
            .header("User-Agent", userAgent)
            .post(json.toRequestBody("application/json".toMediaType()))
            .build(),
    )

private suspend fun OkHttpClient.send(request: Request): HttpResult {
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
    return response.use { HttpResult(it.code, it.body.string()) }
}

internal const val HTTP_TOO_MANY = 429
private const val HTTP_OK = 200
private const val HTTP_LAST_OK = 299
private val HTTP_OK_RANGE = HTTP_OK..HTTP_LAST_OK
