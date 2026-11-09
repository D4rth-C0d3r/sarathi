package io.github.d4rthc0d3r.sarathi.maps

import android.content.Context
import android.util.AtomicFile
import io.github.d4rthc0d3r.sarathi.core.maps.DeskRecording
import kotlinx.serialization.json.Json
import java.io.File

/** The one desk-test recording (step F8), as JSON in the app's private storage. */
object DeskRecordings {
    private val JSON = Json { ignoreUnknownKeys = true }

    private fun file(context: Context) =
        AtomicFile(
            File(
                File(context.filesDir, "desk").apply {
                    mkdirs()
                },
                "recording.json",
            ),
        )

    fun load(context: Context): DeskRecording? =
        runCatching {
            JSON.decodeFromString(DeskRecording.serializer(), file(context).readFully().decodeToString())
        }.getOrNull()

    fun save(
        context: Context,
        recording: DeskRecording,
    ) {
        val f = file(context)
        val out = f.startWrite()
        runCatching {
            out.write(JSON.encodeToString(DeskRecording.serializer(), recording).encodeToByteArray())
            f.finishWrite(out)
        }.onFailure { f.failWrite(out) }
    }
}
