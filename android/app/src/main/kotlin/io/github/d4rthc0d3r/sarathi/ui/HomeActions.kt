package io.github.d4rthc0d3r.sarathi.ui

import io.github.d4rthc0d3r.sarathi.core.protocol.Lock

/** What the rider can do on the home screen. */
data class HomeActions(
    val onSetupStep: (SetupStep) -> Unit = {},
    val onStartLink: () -> Unit = {},
    val onStopLink: () -> Unit = {},
    val onAutoStart: (Boolean) -> Unit = {},
    val onForget: () -> Unit = {},
    val onLock: (Lock) -> Unit = {},
)
