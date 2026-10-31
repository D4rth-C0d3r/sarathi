package io.github.d4rthc0d3r.sarathi.core.protocol

import java.io.File

/** Reads the shared golden vectors in docs/protocol/vectors (path set by the Gradle test task). */
object Vectors {
    private val dir = File(checkNotNull(System.getProperty("sarathi.vectorsDir")) { "sarathi.vectorsDir not set" })

    fun load(name: String): JsonObject = StrictJsonParser.parse(File(dir, name).readBytes()) as JsonObject
}

fun JsonValue?.obj(): JsonObject = this as JsonObject

fun JsonValue?.list(): List<JsonValue> = (this as JsonArray).items

fun JsonValue?.str(): String = (this as JsonString).value

fun JsonValue?.long(): Long = checkNotNull((this as JsonNumber).toLongOrNull())

fun JsonValue?.int(): Int = long().toInt()

fun String.hexBytes(): ByteArray = chunked(2).map { it.toInt(16).toByte() }.toByteArray()

fun ByteArray.hex(): String = joinToString("") { "%02x".format(it) }
