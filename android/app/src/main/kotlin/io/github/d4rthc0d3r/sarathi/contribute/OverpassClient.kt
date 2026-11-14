package io.github.d4rthc0d3r.sarathi.contribute

import android.util.Log
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.Gap
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.Overpass
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.RoadIndex
import kotlinx.coroutines.delay
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import okhttp3.FormBody
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.IOException
import java.util.concurrent.TimeUnit

/**
 * Current OSM data around a gap, from Overpass (ADR-0012 §3; its usage rules): one request at a
 * time, our own User-Agent. When it's busy (429, or 502-504 at capacity) it backs off: retries
 * after 15 s, then 30 s. Call it off the main thread.
 */
class OverpassClient(
    http: OkHttpClient,
    private val userAgent: String,
) {
    private val client = http.newBuilder().readTimeout(READ_TIMEOUT_S, TimeUnit.SECONDS).build()
    private val one = Mutex()

    /** Roads and car parks near [gap], or null if Overpass couldn't answer now. */
    suspend fun around(gap: Gap): RoadIndex? =
        one.withLock {
            var answer = request(gap)
            for (wait in BACKOFF_MS) {
                if (answer != Answer.Busy) break
                Log.i(TAG, "overpass busy: trying again in ${wait / MS_PER_S} s")
                delay(wait)
                answer = request(gap)
            }
            (answer as? Answer.Roads)?.index
        }

    private sealed interface Answer {
        data class Roads(
            val index: RoadIndex,
        ) : Answer

        data object Busy : Answer

        data object Failed : Answer
    }

    private fun request(gap: Gap): Answer {
        val request =
            Request
                .Builder()
                .url(Overpass.URL)
                .header("User-Agent", userAgent)
                .post(FormBody.Builder().add("data", Overpass.query(gap)).build())
                .build()
        return try {
            client.newCall(request).execute().use { r ->
                when {
                    r.code in BUSY -> Answer.Busy
                    r.isSuccessful -> Answer.Roads(Overpass.parse(r.body.byteStream()))
                    else -> Answer.Failed.also { Log.w(TAG, "overpass: HTTP ${r.code}") }
                }
            }
        } catch (e: IOException) {
            Log.w(TAG, "overpass: ${e.message}")
            Answer.Failed
        } catch (e: org.xml.sax.SAXException) {
            Log.w(TAG, "overpass: unreadable answer: ${e.message}")
            Answer.Failed
        }
    }

    private companion object {
        const val TAG = "Contribute"
        const val READ_TIMEOUT_S = 40L // the query asks for at most 25 s
        val BACKOFF_MS = listOf(15_000L, 30_000L)
        const val MS_PER_S = 1000

        // Too many requests; bad gateway, unavailable, gateway timeout: the server is at capacity.
        val BUSY = setOf(429, 502, 503, 504)
    }
}
