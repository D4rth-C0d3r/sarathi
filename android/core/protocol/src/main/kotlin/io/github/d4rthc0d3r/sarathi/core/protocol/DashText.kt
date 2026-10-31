package io.github.d4rthc0d3r.sarathi.core.protocol

import java.text.Normalizer

/**
 * Text for the dash (protocol.md section 3, "Text the device shows"): its fonts have printable
 * ASCII plus ° and •, so accents are stripped (é becomes e), © becomes (c), dashes, curly
 * quotes and ellipses become their ASCII forms, and anything else
 * is dropped; then it's cut to [maxBytes] of UTF-8 on a character boundary, with spaces
 * collapsed. A name with nothing left (all Devanagari, say) becomes empty.
 */
object DashText {
    private val COMBINING = Regex("\\p{Mn}+")
    private val SPACES = Regex("\\s+")
    private const val FIRST_PRINTABLE = ' '
    private const val LAST_PRINTABLE = '~'
    private val KEPT = setOf('°', '•')

    // Punctuation common in titles and names, as the nearest ASCII.
    private val ASCII =
        mapOf(
            "©" to "(c)",
            "–" to "-",
            "—" to "-",
            "‘" to "'",
            "’" to "'",
            "“" to "\"",
            "”" to "\"",
            "…" to "...",
        )

    fun of(
        text: String,
        maxBytes: Int,
    ): String {
        val plain =
            Normalizer
                .normalize(ASCII.entries.fold(text) { acc, (from, to) -> acc.replace(from, to) }, Normalizer.Form.NFD)
                .replace(COMBINING, "")
                .filter { it in FIRST_PRINTABLE..LAST_PRINTABLE || it in KEPT || it.isWhitespace() }
                .replace(SPACES, " ")
                .trim()
        return cut(plain, maxBytes)
    }

    private fun cut(
        text: String,
        maxBytes: Int,
    ): String {
        var bytes = 0
        val out = StringBuilder()
        for (c in text) {
            bytes += c.toString().toByteArray(Charsets.UTF_8).size
            if (bytes > maxBytes) break
            out.append(c)
        }
        return out.toString().trimEnd()
    }
}
