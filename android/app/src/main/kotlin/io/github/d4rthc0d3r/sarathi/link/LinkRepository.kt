package io.github.d4rthc0d3r.sarathi.link

import io.github.d4rthc0d3r.sarathi.core.ble.LinkState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** What the ride link service is doing, for the UI. One per process. */
object LinkRepository {
    data class Status(
        val running: Boolean = false,
        val link: LinkState = LinkState.Stopped,
        val locationActive: Boolean = false,
        val locationFixes: Int = 0,
        val lastFixAtMs: Long = 0,
    )

    private val _status = MutableStateFlow(Status())
    val status: StateFlow<Status> = _status.asStateFlow()

    internal fun update(change: (Status) -> Status) {
        _status.value = change(_status.value)
    }
}
