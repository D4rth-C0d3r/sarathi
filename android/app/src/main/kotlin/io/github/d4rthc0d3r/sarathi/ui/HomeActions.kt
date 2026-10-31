package io.github.d4rthc0d3r.sarathi.ui

/** What the rider can do on the home screen. */
data class HomeActions(
    val onSetupStep: (SetupStep) -> Unit = {},
    val onStartLink: () -> Unit = {},
    val onStopLink: () -> Unit = {},
    val onAutoStart: (Boolean) -> Unit = {},
    val onForget: () -> Unit = {},
)
