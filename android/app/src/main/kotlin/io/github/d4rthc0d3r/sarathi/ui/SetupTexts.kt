package io.github.d4rthc0d3r.sarathi.ui

import androidx.annotation.StringRes
import io.github.d4rthc0d3r.sarathi.R

/** Title, reason and button text for each setup step. Reasons say what the rider gets. */
object SetupTexts {
    data class Texts(
        @param:StringRes val title: Int,
        @param:StringRes val reason: Int,
        @param:StringRes val action: Int,
    )

    fun of(step: SetupStep): Texts =
        when (step) {
            SetupStep.NEARBY -> {
                Texts(R.string.setup_nearby, R.string.setup_nearby_reason, R.string.allow)
            }

            SetupStep.NOTIFICATIONS -> {
                Texts(R.string.setup_notifications, R.string.setup_notifications_reason, R.string.allow)
            }

            SetupStep.LOCATION -> {
                Texts(R.string.setup_location, R.string.setup_location_reason, R.string.allow)
            }

            SetupStep.BACKGROUND_LOCATION -> {
                Texts(
                    R.string.setup_background_location,
                    R.string.setup_background_location_reason,
                    R.string.open_settings,
                )
            }

            SetupStep.BATTERY -> {
                Texts(R.string.setup_battery, R.string.setup_battery_reason, R.string.allow)
            }

            SetupStep.PAIR -> {
                Texts(R.string.setup_pair, R.string.setup_pair_reason, R.string.find_dash)
            }
        }
}
