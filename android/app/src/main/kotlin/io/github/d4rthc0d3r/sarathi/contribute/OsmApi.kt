package io.github.d4rthc0d3r.sarathi.contribute

import android.util.Log
import io.github.d4rthc0d3r.sarathi.BuildConfig
import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.OsmData
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.FormBody
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.MultipartBody
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONException
import org.json.JSONObject
import java.io.IOException
import java.util.Locale

/** How an uploaded trace shows on OSM: the two kinds OSM still accepts (ADR-0012 §4). */
enum class TraceVisibility(
    val api: String,
) {
    /** Listed publicly, linked to the rider's account, with times. */
    IDENTIFIABLE("identifiable"),

    /** Not listed and not linked to the account; the points and times are still public. */
    TRACKABLE("trackable"),
}

/** The result of sending something to OSM. */
sealed interface Sent {
    data class Ok(
        val id: Long,
    ) : Sent

    /** OSM no longer accepts the login: the rider must sign in again. */
    data object SignedOut : Sent

    data class Failed(
        val why: String,
    ) : Sent
}

/**
 * The OSM API calls the helper makes, each one confirmed by the rider first (ADR-0012 §4):
 * a GPS trace upload and a note. The rider's own login; our User-Agent; one at a time.
 */
class OsmApi(
    private val http: OkHttpClient,
    private val token: String,
) {
    suspend fun uploadTrace(
        gpx: String,
        description: String,
        visibility: TraceVisibility,
    ): Sent {
        val body =
            MultipartBody
                .Builder()
                .setType(MultipartBody.FORM)
                .addFormDataPart("file", "sarathi.gpx", gpx.toRequestBody("application/gpx+xml".toMediaType()))
                .addFormDataPart("description", description)
                .addFormDataPart("tags", "sarathi")
                .addFormDataPart("visibility", visibility.api)
                .build()
        return send("/api/0.6/gpx", body) { it.trim().toLongOrNull() }
    }

    suspend fun addNote(
        at: GeoPoint,
        text: String,
    ): Sent {
        val body =
            FormBody
                .Builder()
                .add("lat", String.format(Locale.ROOT, "%.6f", at.lat))
                .add("lon", String.format(Locale.ROOT, "%.6f", at.lng))
                .add("text", text)
                .build()
        return send("/api/0.6/notes.json", body) { answer ->
            try {
                JSONObject(answer).getJSONObject("properties").getLong("id")
            } catch (e: JSONException) {
                Log.w(TAG, "osm note: unreadable answer: ${e.message}")
                null
            }
        }
    }

    /** Current OSM data in a small box, for a guided edit (the editing API's purpose). */
    suspend fun map(
        south: Double,
        west: Double,
        north: Double,
        east: Double,
    ): OsmData? =
        withContext(Dispatchers.IO) {
            val box = String.format(Locale.ROOT, "%.6f,%.6f,%.6f,%.6f", west, south, east, north)
            val request = authorized("/api/0.6/map?bbox=$box").get().build()
            try {
                http.newCall(request).execute().use { r ->
                    if (r.isSuccessful) {
                        OsmData.parse(
                            r.body.byteStream(),
                        )
                    } else {
                        null.also { Log.w(TAG, "osm map: HTTP ${r.code}") }
                    }
                }
            } catch (e: IOException) {
                Log.w(TAG, "osm map: ${e.message}")
                null
            } catch (e: org.xml.sax.SAXException) {
                Log.w(TAG, "osm map: unreadable: ${e.message}")
                null
            }
        }

    /**
     * One guided edit in its own changeset (ADR-0012 §5): opened with [changeset]'s tags, the
     * osmChange from [change] uploaded, and closed again whatever happened. Ok carries the
     * changeset's id.
     */
    suspend fun edit(
        changeset: String,
        change: (Long) -> String,
    ): Sent {
        val opened =
            send("/api/0.6/changeset/create", changeset.toRequestBody(XML), put = true) { it.trim().toLongOrNull() }
        if (opened !is Sent.Ok) return opened
        val uploaded =
            send("/api/0.6/changeset/${opened.id}/upload", change(opened.id).toRequestBody(XML)) { opened.id }
        send("/api/0.6/changeset/${opened.id}/close", "".toRequestBody(XML), put = true) { opened.id }
        return uploaded
    }

    private fun authorized(path: String) =
        Request
            .Builder()
            .url(BuildConfig.OSM_SERVER + path)
            .header("User-Agent", USER_AGENT)
            .header("Authorization", "Bearer $token")

    private suspend fun send(
        path: String,
        body: RequestBody,
        put: Boolean = false,
        id: (String) -> Long?,
    ): Sent =
        withContext(Dispatchers.IO) {
            val request = authorized(path).let { if (put) it.put(body) else it.post(body) }.build()
            try {
                http.newCall(request).execute().use { r ->
                    when {
                        r.code == UNAUTHORIZED || r.code == FORBIDDEN -> Sent.SignedOut
                        r.code == CONFLICT -> Sent.Failed(CHANGED_MEANWHILE)
                        r.isSuccessful -> id(r.body.string())?.let { Sent.Ok(it) } ?: Sent.Failed("no id in the answer")
                        else -> Sent.Failed("HTTP ${r.code}").also { Log.w(TAG, "osm $path: HTTP ${r.code}") }
                    }
                }
            } catch (e: IOException) {
                Log.w(TAG, "osm $path: ${e.message}")
                Sent.Failed(e.message ?: "network")
            }
        }

    companion object {
        /** The app and its version, as OSM's API policy asks; nothing personal (ADR-0012 §5). */
        val USER_AGENT = "sarathi/${BuildConfig.VERSION_NAME}"
        private const val TAG = "Contribute"
        private const val UNAUTHORIZED = 401
        private const val FORBIDDEN = 403
        private const val CONFLICT = 409
        private val XML = "text/xml".toMediaType()

        /** OSM refused the edit: something there changed since the download. */
        const val CHANGED_MEANWHILE = "OpenStreetMap changed there meanwhile"
    }
}
