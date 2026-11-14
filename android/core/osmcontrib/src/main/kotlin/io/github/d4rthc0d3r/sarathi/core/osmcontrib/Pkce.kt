package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import java.security.MessageDigest
import java.security.SecureRandom
import java.util.Base64

/** OAuth 2 with PKCE (RFC 7636) for the rider's OSM login (ADR-0012 §5): S256 only. */
object Pkce {
    private const val VERIFIER_BYTES = 32 // 43 characters in base64url
    private val random = SecureRandom()
    private val base64url = Base64.getUrlEncoder().withoutPadding()

    /** A fresh code verifier, or `state` value: 32 random bytes, base64url. */
    fun newSecret(): String = ByteArray(VERIFIER_BYTES).also(random::nextBytes).let(base64url::encodeToString)

    /** The S256 challenge for [verifier]. */
    fun challenge(verifier: String): String =
        base64url.encodeToString(MessageDigest.getInstance("SHA-256").digest(verifier.toByteArray(Charsets.US_ASCII)))
}
