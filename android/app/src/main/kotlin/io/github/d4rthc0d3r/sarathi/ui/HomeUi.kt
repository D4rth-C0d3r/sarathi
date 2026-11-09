package io.github.d4rthc0d3r.sarathi.ui

import io.github.d4rthc0d3r.sarathi.core.maps.GuidanceState
import io.github.d4rthc0d3r.sarathi.core.protocol.Lock
import io.github.d4rthc0d3r.sarathi.link.LinkRepository

/** What Home shows: setup, the ride link, the moving lock, and the ride in progress. */
data class HomeUi(
    val setup: SetupState,
    val link: LinkRepository.Status,
    val lock: Lock,
    val ride: RideStatus? = null,
)

/** The ride in progress, for Home. */
data class RideStatus(
    val destination: String,
    val guidance: GuidanceState?,
)
