package io.github.d4rthc0d3r.sarathi.core.links

import java.net.URI

/**
 * Follows a short link by its redirects only (ADR-0008): at most [MAX_HOPS], each hop's host on
 * the allow-list, no page content read. [location] fetches one hop's `Location` header.
 */
object Redirects {
    const val MAX_HOPS = 5
    private val GOOGLE = Regex("""^(www\.|maps\.)?google\.[a-z.]+$""")
    private val ALLOWED = setOf("maps.app.goo.gl", "goo.gl")

    fun allowed(url: String): Boolean {
        val host = runCatching { URI(url).host?.lowercase() }.getOrNull() ?: return false
        return url.startsWith("https://", ignoreCase = true) && (host in ALLOWED || GOOGLE.matches(host))
    }

    suspend fun resolve(
        short: SharedLink.Short,
        text: String,
        location: suspend (String) -> String?,
    ): SharedLink? {
        var url: String? = short.url
        var result: SharedLink? = short
        var hops = 0
        while (result is SharedLink.Short && hops < MAX_HOPS) {
            val from = url?.takeIf(::allowed)
            val next = from?.let { location(it) }
            url = next?.let { runCatching { URI(from).resolve(it).toString() }.getOrDefault(it) }?.takeIf(::allowed)
            result = url?.let { LinkParser.parseUrl(it, text) }
            hops++
        }
        return result.takeUnless { it is SharedLink.Short }
    }
}
