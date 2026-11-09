package io.github.d4rthc0d3r.sarathi.ui

import io.github.d4rthc0d3r.sarathi.core.maps.MeterStatus

/** Key presence and usage meters for the screen; values only, never the keys. */
data class MapsStatus(
    val googleKey: Boolean = false,
    /** Google's Navigation SDK terms, accepted once on this phone. */
    val googleTerms: Boolean = false,
    val graphHopperKey: Boolean = false,
    val mapplsConfigured: Boolean = false,
    val meters: List<MeterStatus> = emptyList(),
    /** Debug builds: the failure drill's switch (step F8); null hides it. */
    val drill: Boolean? = null,
    /** Debug builds: the map data desk check's result (step G2); null hides it. */
    val mapCheck: String? = null,
)
