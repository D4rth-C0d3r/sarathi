package io.github.d4rthc0d3r.sarathi.maps

import android.content.Context
import android.util.AtomicFile
import io.github.d4rthc0d3r.sarathi.BuildConfig
import io.github.d4rthc0d3r.sarathi.core.maps.CostGuard
import io.github.d4rthc0d3r.sarathi.core.maps.MapsSettings
import io.github.d4rthc0d3r.sarathi.core.maps.Usage
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.serialization.KSerializer
import kotlinx.serialization.json.Json
import java.io.File

/**
 * The maps settings and the cost guard's counts (ADR-0006), as small JSON files in the app's
 * private storage. One per process ([get]).
 */
class MapsStore private constructor(
    context: Context,
) {
    private val dir = File(context.filesDir, "maps").apply { mkdirs() }
    private val settingsFile = AtomicFile(File(dir, "settings.json"))
    private val usageFile = AtomicFile(File(dir, "usage.json"))

    private val _settings = MutableStateFlow(read(settingsFile, MapsSettings.serializer()) ?: MapsSettings())
    val settings: StateFlow<MapsSettings> = _settings.asStateFlow()

    private val _usage = MutableStateFlow(read(usageFile, Usage.serializer()) ?: Usage())

    /** Changes with every count, for the usage meters. */
    val usage: StateFlow<Usage> = _usage.asStateFlow()

    var guard = newGuard()
        private set

    fun update(change: (MapsSettings) -> MapsSettings) {
        val next = change(_settings.value)
        _settings.value = next
        write(settingsFile, MapsSettings.serializer(), next)
        guard = newGuard() // Mappls's caps come from the settings
    }

    private fun newGuard() =
        CostGuard(CostGuard.caps(_settings.value), initial = _usage.value) {
            _usage.value = it
            write(usageFile, Usage.serializer(), it)
        }

    private fun <T> read(
        file: AtomicFile,
        serializer: KSerializer<T>,
    ): T? = runCatching { JSON.decodeFromString(serializer, file.readFully().decodeToString()) }.getOrNull()

    private fun <T> write(
        file: AtomicFile,
        serializer: KSerializer<T>,
        value: T,
    ) {
        val out = file.startWrite()
        runCatching {
            out.write(JSON.encodeToString(serializer, value).encodeToByteArray())
            file.finishWrite(out)
        }.onFailure { file.failWrite(out) }
    }

    companion object {
        private val JSON = Json { ignoreUnknownKeys = true }

        @Volatile private var instance: MapsStore? = null

        fun get(context: Context): MapsStore =
            instance ?: synchronized(this) { instance ?: MapsStore(context.applicationContext).also { instance = it } }
    }
}

/** Which keys this build has (ADR-0006, amended): built in from ~/sarathi-secrets, never shown. */
object Keys {
    val google: String? get() = BuildConfig.GOOGLE_API_KEY.ifEmpty { null }
    val graphHopper: String? get() = BuildConfig.GRAPHHOPPER_API_KEY.ifEmpty { null }
}
