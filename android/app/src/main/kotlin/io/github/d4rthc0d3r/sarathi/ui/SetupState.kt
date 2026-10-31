package io.github.d4rthc0d3r.sarathi.ui

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.PowerManager
import androidx.core.app.NotificationManagerCompat
import io.github.d4rthc0d3r.sarathi.feature.calls.CallsFeature
import io.github.d4rthc0d3r.sarathi.link.DashAssociation

/** Each setup step, in the order the app asks for them. */
enum class SetupStep {
    NEARBY,
    NOTIFICATIONS,
    LOCATION,
    BACKGROUND_LOCATION,
    BATTERY,
    PAIR,
    MUSIC,
    CALLS,
}

/** What's done, read fresh from Android every time the app comes to the front. */
data class SetupState(
    val done: Set<SetupStep> = emptySet(),
    val dashAddress: String? = null,
    val autoStartSupported: Boolean = false,
    val autoStart: Boolean = false,
) {
    val next: SetupStep? get() = SetupStep.entries.firstOrNull { it !in done }

    companion object {
        fun read(context: Context): SetupState {
            val association = DashAssociation(context)
            val done = SetupStep.entries.filter { isDone(context, it, association) }.toSet()
            return SetupState(done, association.address(), association.autoStartSupported, association.autoStart)
        }

        private fun isDone(
            context: Context,
            step: SetupStep,
            association: DashAssociation,
        ): Boolean =
            when (step) {
                SetupStep.NEARBY -> {
                    granted(context, Manifest.permission.BLUETOOTH_CONNECT, Build.VERSION_CODES.S)
                }

                SetupStep.NOTIFICATIONS -> {
                    granted(
                        context,
                        Manifest.permission.POST_NOTIFICATIONS,
                        Build.VERSION_CODES.TIRAMISU,
                    )
                }

                SetupStep.LOCATION -> {
                    granted(context, Manifest.permission.ACCESS_FINE_LOCATION, 1)
                }

                SetupStep.BACKGROUND_LOCATION -> {
                    granted(
                        context,
                        Manifest.permission.ACCESS_BACKGROUND_LOCATION,
                        Build.VERSION_CODES.Q,
                    )
                }

                SetupStep.BATTERY -> {
                    context
                        .getSystemService(
                            PowerManager::class.java,
                        ).isIgnoringBatteryOptimizations(context.packageName)
                }

                SetupStep.PAIR -> {
                    association.address() != null
                }

                SetupStep.MUSIC -> {
                    NotificationManagerCompat.getEnabledListenerPackages(context).contains(context.packageName)
                }

                SetupStep.CALLS -> {
                    CallsFeature.allowed(context)
                }
            }

        /** Granted, or not needed before [since] (Android grants it at install there). */
        private fun granted(
            context: Context,
            permission: String,
            since: Int,
        ) = Build.VERSION.SDK_INT < since ||
            context.checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED
    }
}
