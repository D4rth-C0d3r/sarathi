package io.github.d4rthc0d3r.sarathi.ui

import androidx.annotation.StringRes
import io.github.d4rthc0d3r.sarathi.R
import io.github.d4rthc0d3r.sarathi.core.maps.Meter

/** A usage meter in words, with its used and cap. */
object MeterNames {
    @StringRes
    fun of(meter: Meter): Int =
        when (meter) {
            Meter.GOOGLE_NAV_DESTINATIONS -> R.string.meter_google_nav
            Meter.GOOGLE_AUTOCOMPLETE -> R.string.meter_google_autocomplete
            Meter.GOOGLE_PLACE_DETAILS -> R.string.meter_google_details
            Meter.GOOGLE_GEOCODING -> R.string.meter_google_geocoding
            Meter.MAPPLS_SEARCH -> R.string.meter_mappls_search
            Meter.MAPPLS_ROUTING -> R.string.meter_mappls_routing
        }
}
