package io.github.d4rthc0d3r.sarathi.link

import androidx.annotation.StringRes
import io.github.d4rthc0d3r.sarathi.R
import io.github.d4rthc0d3r.sarathi.core.ble.LinkProblem
import io.github.d4rthc0d3r.sarathi.core.ble.LinkState

/** The link state in words, for the notification and the status screen. */
object LinkText {
    @StringRes
    fun of(state: LinkState): Int =
        when (state) {
            LinkState.Stopped -> R.string.link_stopped
            LinkState.Waiting -> R.string.link_waiting
            LinkState.Pairing -> R.string.link_pairing
            LinkState.Connecting -> R.string.link_connecting
            is LinkState.Ready -> R.string.link_ready
            is LinkState.Incompatible -> if (state.updateApp) R.string.link_update_app else R.string.link_update_dash
            is LinkState.Failed -> problem(state.problem)
        }

    @StringRes
    private fun problem(problem: LinkProblem): Int =
        when (problem) {
            LinkProblem.NO_PERMISSION -> R.string.problem_no_permission
            LinkProblem.BLUETOOTH_OFF -> R.string.problem_bluetooth_off
            LinkProblem.PAIRING_FAILED -> R.string.problem_pairing_failed
            LinkProblem.STALE_PAIRING -> R.string.problem_stale_pairing
            LinkProblem.SETUP_FAILED, LinkProblem.OTHER -> R.string.problem_other
        }
}
