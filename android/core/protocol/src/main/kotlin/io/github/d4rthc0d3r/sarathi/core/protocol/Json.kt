package io.github.d4rthc0d3r.sarathi.core.protocol

import java.nio.ByteBuffer
import java.nio.charset.CharacterCodingException
import java.nio.charset.CodingErrorAction

/**
 * A parsed JSON value. Objects keep their members in document order. Numbers keep their
 * text, so nothing is rounded before a codec checks it.
 */
sealed interface JsonValue

data class JsonObject(
    val members: Map<String, JsonValue>,
) : JsonValue {
    operator fun get(name: String): JsonValue? = members[name]
}

data class JsonArray(
    val items: List<JsonValue>,
) : JsonValue

data class JsonString(
    val value: String,
) : JsonValue

data class JsonNumber(
    val text: String,
) : JsonValue {
    /** The value if this is a whole number within ±2^53 written without fraction or exponent. */
    fun toLongOrNull(): Long? {
        val digits = text.removePrefix("-")
        val whole = digits.isNotEmpty() && digits.all { it in '0'..'9' } && digits.length <= MAX_DIGITS
        return if (whole) text.toLong().takeIf { it in -MAX_INTEGER..MAX_INTEGER } else null
    }

    private companion object {
        const val MAX_DIGITS = 16
        const val MAX_INTEGER = 1L shl 53
    }
}

data class JsonBool(
    val value: Boolean,
) : JsonValue

data object JsonNull : JsonValue

/** Thrown for text that isn't one valid JSON value. */
class JsonSyntaxException(
    message: String,
    cause: Throwable? = null,
) : Exception(message, cause)

/** Thrown for valid JSON with a name repeated in one object. */
class JsonDuplicateNameException(
    name: String,
) : Exception("repeated name: $name")

/**
 * Strict RFC 8259 parser, as strict as the firmware's JsonReader: valid UTF-8, one value with
 * nothing after it, no trailing commas, no leading zeros, no raw control characters in strings,
 * and no repeated names in an object. Escaped surrogates are decoded as given; codecs reject
 * unpaired ones in values they read.
 */
@Suppress("TooManyFunctions") // one small function per grammar rule; splitting would scatter it
class StrictJsonParser private constructor(
    private val s: String,
) {
    private var pos = 0

    companion object {
        private const val MAX_DEPTH = 64
        private const val HEX_RADIX = 16
        private const val ESCAPE_HEX_DIGITS = 4
        private const val FIRST_PRINTABLE = 0x20

        fun parse(bytes: ByteArray): JsonValue = parse(decodeUtf8(bytes))

        fun parse(text: String): JsonValue {
            val parser = StrictJsonParser(text)
            parser.skipSpace()
            val value = parser.value(0)
            parser.skipSpace()
            if (parser.pos != text.length) throw JsonSyntaxException("text after the value")
            return value
        }

        private fun decodeUtf8(bytes: ByteArray): String =
            try {
                Charsets.UTF_8
                    .newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes))
                    .toString()
            } catch (e: CharacterCodingException) {
                throw JsonSyntaxException("not UTF-8", e)
            }
    }

    private fun peek(): Char = if (pos < s.length) s[pos] else '\u0000'

    private fun fail(what: String): Nothing = throw JsonSyntaxException("$what at $pos")

    private fun skipSpace() {
        while (pos < s.length && s[pos] in " \t\r\n") pos++
    }

    private fun expect(c: Char) {
        if (peek() != c) fail("expected '$c'")
        pos++
    }

    private fun value(depth: Int): JsonValue {
        if (depth > MAX_DEPTH) fail("too deep")
        return when (peek()) {
            '{' -> obj(depth)
            '[' -> array(depth)
            '"' -> JsonString(string())
            't' -> literal("true", JsonBool(true))
            'f' -> literal("false", JsonBool(false))
            'n' -> literal("null", JsonNull)
            else -> number()
        }
    }

    private fun obj(depth: Int): JsonObject {
        expect('{')
        val members = LinkedHashMap<String, JsonValue>()
        skipSpace()
        if (peek() == '}') {
            pos++
            return JsonObject(members)
        }
        do {
            skipSpace()
            val name = string()
            skipSpace()
            expect(':')
            skipSpace()
            if (members.put(name, value(depth + 1)) != null) throw JsonDuplicateNameException(name)
            skipSpace()
        } while (next('}'))
        return JsonObject(members)
    }

    private fun array(depth: Int): JsonArray {
        expect('[')
        val items = mutableListOf<JsonValue>()
        skipSpace()
        if (peek() == ']') {
            pos++
            return JsonArray(items)
        }
        do {
            skipSpace()
            items += value(depth + 1)
            skipSpace()
        } while (next(']'))
        return JsonArray(items)
    }

    /** After a member or element: true for a comma (more follow), false for `close`. */
    private fun next(close: Char): Boolean {
        val c = peek()
        if (c != ',' && c != close) fail("expected ',' or '$close'")
        pos++
        return c == ','
    }

    private fun string(): String {
        expect('"')
        val out = StringBuilder()
        while (true) {
            if (pos >= s.length) fail("unterminated string")
            val c = s[pos++]
            when {
                c == '"' -> return out.toString()
                c == '\\' -> out.append(escape())
                c.code < FIRST_PRINTABLE -> fail("raw control character")
                else -> out.append(c)
            }
        }
    }

    private fun escape(): Char {
        if (pos >= s.length) fail("unterminated escape")
        return when (val e = s[pos++]) {
            '"', '\\', '/' -> e
            'b' -> '\b'
            'f' -> '\u000C'
            'n' -> '\n'
            'r' -> '\r'
            't' -> '\t'
            'u' -> hex4()
            else -> fail("bad escape")
        }
    }

    private fun hex4(): Char {
        if (pos + ESCAPE_HEX_DIGITS > s.length) fail("short \\u escape")
        val digits = s.substring(pos, pos + ESCAPE_HEX_DIGITS)
        if (!digits.all { Character.digit(it, HEX_RADIX) >= 0 }) fail("bad \\u escape")
        pos += ESCAPE_HEX_DIGITS
        return digits.toInt(HEX_RADIX).toChar()
    }

    private fun literal(
        word: String,
        result: JsonValue,
    ): JsonValue {
        if (!s.startsWith(word, pos)) fail("bad literal")
        pos += word.length
        return result
    }

    // -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)?
    private fun number(): JsonNumber {
        val start = pos
        if (peek() == '-') pos++
        when {
            peek() == '0' -> pos++
            peek() in '1'..'9' -> digits()
            else -> fail("bad value")
        }
        if (peek() == '.') {
            pos++
            if (peek() !in '0'..'9') fail("bad fraction")
            digits()
        }
        if (peek() == 'e' || peek() == 'E') {
            pos++
            if (peek() == '+' || peek() == '-') pos++
            if (peek() !in '0'..'9') fail("bad exponent")
            digits()
        }
        return JsonNumber(s.substring(start, pos))
    }

    private fun digits() {
        while (peek() in '0'..'9') pos++
    }
}

/**
 * Writes compact JSON in the canonical form of protocol.md section 3: no whitespace, plain
 * decimal integers, raw UTF-8 strings escaping only `"`, `\` and U+0000 to U+001F. Keys come
 * out in the order they are written.
 */
@Suppress("TooManyFunctions") // one call per JSON construct
class CanonicalJsonWriter {
    private val out = StringBuilder()
    private val hasItems = ArrayDeque<Boolean>()
    private var afterKey = false

    fun beginObject() = open('{')

    fun endObject() = close('}')

    fun beginArray() = open('[')

    fun endArray() = close(']')

    fun key(name: String) {
        string(name)
        out.append(':')
        afterKey = true
    }

    fun string(text: String) {
        separator()
        out.append('"')
        for (c in text) appendEscaped(c)
        out.append('"')
    }

    fun number(value: Long) {
        separator()
        out.append(value)
    }

    fun bool(value: Boolean) {
        separator()
        out.append(value)
    }

    fun nul() {
        separator()
        out.append("null")
    }

    fun toBytes(): ByteArray {
        check(hasItems.isEmpty()) { "unbalanced JSON" }
        return out.toString().toByteArray(Charsets.UTF_8)
    }

    private fun open(c: Char) {
        separator()
        out.append(c)
        hasItems.addLast(false)
    }

    private fun close(c: Char) {
        hasItems.removeLast()
        out.append(c)
    }

    private fun separator() {
        if (afterKey) {
            afterKey = false
            return
        }
        if (hasItems.isNotEmpty()) {
            if (hasItems.last()) out.append(',')
            hasItems[hasItems.lastIndex] = true
        }
    }

    private fun appendEscaped(c: Char) {
        when (c) {
            '"' -> {
                out.append("\\\"")
            }

            '\\' -> {
                out.append("\\\\")
            }

            '\b' -> {
                out.append("\\b")
            }

            '\t' -> {
                out.append("\\t")
            }

            '\n' -> {
                out.append("\\n")
            }

            '\u000C' -> {
                out.append("\\f")
            }

            '\r' -> {
                out.append("\\r")
            }

            else -> {
                if (c.code < CONTROL_LIMIT) {
                    out.append("\\u00").append(HEX[c.code shr NIBBLE]).append(HEX[c.code and NIBBLE_MASK])
                } else {
                    out.append(c)
                }
            }
        }
    }

    private companion object {
        const val CONTROL_LIMIT = 0x20
        const val NIBBLE = 4
        const val NIBBLE_MASK = 0x0F
        const val HEX = "0123456789abcdef"
    }
}
