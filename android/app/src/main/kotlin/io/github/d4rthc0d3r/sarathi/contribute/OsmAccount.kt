package io.github.d4rthc0d3r.sarathi.contribute

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.util.Log
import androidx.core.content.edit
import io.github.d4rthc0d3r.sarathi.BuildConfig
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.Pkce
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.withContext
import okhttp3.FormBody
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONException
import org.json.JSONObject
import java.io.IOException

/**
 * The rider's own OSM account (ADR-0012 §5): OAuth 2 with PKCE in the browser, back through
 * `sarathi://osm-oauth`. The token is kept encrypted with a key in the Android Keystore and is
 * never logged or shown.
 */
object OsmAccount {
    private const val TAG = "Contribute"
    private const val PREFS = "osm_account"
    private const val TOKEN = "token"
    private const val VERIFIER = "pending_verifier"
    private const val STATE = "pending_state"
    const val REDIRECT = "sarathi://osm-oauth"
    const val SCOPES = "write_gpx write_api write_notes"

    private var state: MutableStateFlow<Boolean>? = null

    fun signedIn(context: Context): StateFlow<Boolean> =
        state ?: MutableStateFlow(token(context) != null).also { state = it }

    /** Whether this build can log in at all (the client ID is a build-time secret). */
    val configured: Boolean get() = BuildConfig.OSM_CLIENT_ID.isNotEmpty()

    /** Opens OSM's login page in the browser. */
    fun startLogin(context: Context) {
        val verifier = Pkce.newSecret()
        val check = Pkce.newSecret()
        prefs(context).edit {
            putString(VERIFIER, verifier)
            putString(STATE, check)
        }
        val uri =
            Uri
                .parse("${BuildConfig.OSM_SERVER}/oauth2/authorize")
                .buildUpon()
                .appendQueryParameter("response_type", "code")
                .appendQueryParameter("client_id", BuildConfig.OSM_CLIENT_ID)
                .appendQueryParameter("redirect_uri", REDIRECT)
                .appendQueryParameter("scope", SCOPES)
                .appendQueryParameter("code_challenge", Pkce.challenge(verifier))
                .appendQueryParameter("code_challenge_method", "S256")
                .appendQueryParameter("state", check)
                .build()
        context.startActivity(Intent(Intent.ACTION_VIEW, uri).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
    }

    /** Finishes the login from OSM's redirect; false if it was refused, stale or failed. */
    suspend fun finishLogin(
        context: Context,
        http: OkHttpClient,
        redirect: Uri,
    ): Boolean {
        val p = prefs(context)
        val verifier = p.getString(VERIFIER, null)
        val expected = p.getString(STATE, null)
        p.edit {
            remove(VERIFIER)
            remove(STATE)
        }
        // The code only counts with our own pending login and the state it was started with.
        val pending = verifier?.takeIf { expected != null && redirect.getQueryParameter("state") == expected }
        val code = redirect.getQueryParameter("code")?.takeIf { pending != null }
        if (pending == null || code == null) {
            Log.w(TAG, "osm login: refused or not ours (${redirect.getQueryParameter("error") ?: "state"})")
            return false
        }
        val token = withContext(Dispatchers.IO) { exchange(http, code, pending) }
        if (token != null) {
            prefs(context).edit { putString(TOKEN, TokenVault.seal(token)) }
            (signedIn(context) as MutableStateFlow).value = true
            Log.i(TAG, "osm login: signed in")
        }
        return token != null
    }

    private fun exchange(
        http: OkHttpClient,
        code: String,
        verifier: String,
    ): String? {
        val body =
            FormBody
                .Builder()
                .add("grant_type", "authorization_code")
                .add("code", code)
                .add("redirect_uri", REDIRECT)
                .add("client_id", BuildConfig.OSM_CLIENT_ID)
                .add("code_verifier", verifier)
                .build()
        val request =
            Request
                .Builder()
                .url("${BuildConfig.OSM_SERVER}/oauth2/token")
                .header("User-Agent", OsmApi.USER_AGENT)
                .post(body)
                .build()
        return try {
            http.newCall(request).execute().use { r ->
                if (!r.isSuccessful) {
                    Log.w(TAG, "osm login: token HTTP ${r.code}")
                    null
                } else {
                    JSONObject(r.body.string()).optString("access_token").takeIf { it.isNotEmpty() }
                }
            }
        } catch (e: IOException) {
            Log.w(TAG, "osm login: ${e.message}")
            null
        } catch (e: JSONException) {
            Log.w(TAG, "osm login: unreadable token answer: ${e.message}")
            null
        }
    }

    /** The access token, or null when signed out (or it can't be decrypted any more). */
    fun token(context: Context): String? = prefs(context).getString(TOKEN, null)?.let(TokenVault::open)

    /** Signs out on this phone and asks OSM to revoke the token. */
    suspend fun signOut(
        context: Context,
        http: OkHttpClient,
    ) {
        val token = token(context)
        forget(context)
        if (token != null) withContext(Dispatchers.IO) { revoke(http, token) }
    }

    /** Drops the token here (also when OSM no longer accepts it). */
    fun forget(context: Context) {
        prefs(context).edit { remove(TOKEN) }
        (signedIn(context) as MutableStateFlow).value = false
    }

    private fun revoke(
        http: OkHttpClient,
        token: String,
    ) {
        val request =
            Request
                .Builder()
                .url("${BuildConfig.OSM_SERVER}/oauth2/revoke")
                .header("User-Agent", OsmApi.USER_AGENT)
                .post(
                    FormBody
                        .Builder()
                        .add("token", token)
                        .add("client_id", BuildConfig.OSM_CLIENT_ID)
                        .build(),
                ).build()
        try {
            http.newCall(request).execute().use { Log.i(TAG, "osm sign-out: revoke HTTP ${it.code}") }
        } catch (e: IOException) {
            Log.w(TAG, "osm sign-out: revoke failed: ${e.message}")
        }
    }

    private fun prefs(context: Context) = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
}
