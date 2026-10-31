package io.github.d4rthc0d3r.sarathi.ui

import io.github.d4rthc0d3r.sarathi.core.maps.MeterStatus

/** Key presence and usage meters for the screen; values only, never the keys. */
data class MapsStatus(
    val googleKey: Boolean = false,
    val graphHopperKey: Boolean = false,
    val mapplsConfigured: Boolean = false,
    val meters: List<MeterStatus> = emptyList(),
)
