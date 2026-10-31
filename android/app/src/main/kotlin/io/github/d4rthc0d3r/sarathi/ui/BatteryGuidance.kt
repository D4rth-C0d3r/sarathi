package io.github.d4rthc0d3r.sarathi.ui

import android.os.Build
import androidx.annotation.StringRes
import io.github.d4rthc0d3r.sarathi.R

/**
 * What to change on this phone so its maker's battery saver doesn't stop the ride link. Android's
 * own exemption (the Battery step) covers stock Android; several makers add their own killers
 * on top (https://dontkillmyapp.com).
 */
object BatteryGuidance {
    @StringRes
    fun forThisPhone(manufacturer: String = Build.MANUFACTURER): Int =
        when (manufacturer.lowercase()) {
            "nothing" -> R.string.oem_nothing
            "samsung" -> R.string.oem_samsung
            "xiaomi", "redmi", "poco" -> R.string.oem_xiaomi
            "oneplus" -> R.string.oem_oneplus
            "oppo", "realme" -> R.string.oem_oppo
            "vivo", "iqoo" -> R.string.oem_vivo
            "huawei", "honor" -> R.string.oem_huawei
            "google" -> R.string.oem_google
            else -> R.string.oem_other
        }
}
