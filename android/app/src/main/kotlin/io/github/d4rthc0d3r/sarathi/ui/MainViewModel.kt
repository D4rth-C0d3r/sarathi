package io.github.d4rthc0d3r.sarathi.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import io.github.d4rthc0d3r.sarathi.core.protocol.Lock
import io.github.d4rthc0d3r.sarathi.link.DashAssociation
import io.github.d4rthc0d3r.sarathi.link.LinkRepository
import io.github.d4rthc0d3r.sarathi.link.LockSettings
import io.github.d4rthc0d3r.sarathi.link.RideLinkService
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Setup progress and the ride link's status; actions that don't need an Activity. */
class MainViewModel(
    application: Application,
) : AndroidViewModel(application) {
    private val _setup = MutableStateFlow(SetupState())
    val setup: StateFlow<SetupState> = _setup.asStateFlow()

    val link: StateFlow<LinkRepository.Status> = LinkRepository.status

    val lock: StateFlow<Lock> = LockSettings.flow(application)

    fun setLock(lock: Lock) = LockSettings.save(getApplication(), lock)

    /** Re-reads permissions and pairing (after any system dialog, and on resume). */
    fun refresh() {
        _setup.value = SetupState.read(getApplication())
    }

    fun startLink() = RideLinkService.start(getApplication())

    fun stopLink() = RideLinkService.stop(getApplication())

    fun setAutoStart(enabled: Boolean) {
        DashAssociation(getApplication()).setAutoStart(enabled)
        refresh()
    }

    /** Forgets the dash here; the rider also forgets the bond in Bluetooth settings and on the dash. */
    fun forgetDash() {
        RideLinkService.stop(getApplication())
        DashAssociation(getApplication()).forget()
        refresh()
    }
}
