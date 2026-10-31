package io.github.d4rthc0d3r.sarathi.feature.destination

import android.content.Context
import android.util.AtomicFile
import io.github.d4rthc0d3r.sarathi.core.maps.PlacesBook
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.serialization.json.Json
import java.io.File

/** Recent destinations and saved places, as JSON in the app's private storage. */
interface PlacesStore {
    val book: StateFlow<PlacesBook>

    fun update(change: (PlacesBook) -> PlacesBook)
}

class FilePlacesStore(
    context: Context,
    nowMs: Long = System.currentTimeMillis(),
) : PlacesStore {
    private val file = AtomicFile(File(File(context.filesDir, "places").apply { mkdirs() }, "book.json"))
    private val _book = MutableStateFlow(read().expire(nowMs))
    override val book: StateFlow<PlacesBook> = _book.asStateFlow()

    override fun update(change: (PlacesBook) -> PlacesBook) {
        val next = change(_book.value)
        _book.value = next
        val out = file.startWrite()
        runCatching {
            out.write(JSON.encodeToString(PlacesBook.serializer(), next).encodeToByteArray())
            file.finishWrite(out)
        }.onFailure { file.failWrite(out) }
    }

    private fun read(): PlacesBook =
        runCatching { JSON.decodeFromString(PlacesBook.serializer(), file.readFully().decodeToString()) }
            .getOrDefault(PlacesBook())

    private companion object {
        val JSON = Json { ignoreUnknownKeys = true }
    }
}
