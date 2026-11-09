package io.github.d4rthc0d3r.sarathi.feature.destination

/** What has to be right before a ride starts (ADR-0008), worked out by the app. */
data class StartChecks(
    val dashPaired: Boolean = false,
    val dashConnected: Boolean = false,
    val backgroundLocation: Boolean = false,
    val batteryExempt: Boolean = false,
    /** The cost guard's line for the backend that routed, e.g. Google's destinations this month. */
    val costLine: String? = null,
    /** Debug builds only: offer a simulated ride for desk tests (step F8). */
    val canSimulate: Boolean = false,
)
