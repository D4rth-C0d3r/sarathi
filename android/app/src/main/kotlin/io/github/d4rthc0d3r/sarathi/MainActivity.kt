package io.github.d4rthc0d3r.sarathi

import android.Manifest
import android.annotation.SuppressLint
import android.content.ComponentName
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.core.content.ContextCompat
import androidx.core.net.toUri
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import io.github.d4rthc0d3r.sarathi.feature.calls.CallsFeature
import io.github.d4rthc0d3r.sarathi.link.DashAssociation
import io.github.d4rthc0d3r.sarathi.link.DashNotificationListener
import io.github.d4rthc0d3r.sarathi.ui.HomeActions
import io.github.d4rthc0d3r.sarathi.ui.HomeScreen
import io.github.d4rthc0d3r.sarathi.ui.MainViewModel
import io.github.d4rthc0d3r.sarathi.ui.SetupStep
import io.github.d4rthc0d3r.sarathi.ui.theme.SarathiTheme

class MainActivity : ComponentActivity() {
    private val model: MainViewModel by viewModels()

    private val permissions =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { model.refresh() }

    private val systemScreen =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { model.refresh() }

    private val chooser =
        registerForActivityResult(ActivityResultContracts.StartIntentSenderForResult()) { result ->
            model.refresh()
            if (result.resultCode == RESULT_OK) model.startLink() // bonds right away: compare the codes
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            SarathiTheme {
                val setup = model.setup.collectAsStateWithLifecycle()
                val link = model.link.collectAsStateWithLifecycle()
                val lock = model.lock.collectAsStateWithLifecycle()
                HomeScreen(
                    setup = setup.value,
                    link = link.value,
                    lock = lock.value,
                    actions =
                        HomeActions(
                            onSetupStep = ::runStep,
                            onStartLink = model::startLink,
                            onStopLink = model::stopLink,
                            onAutoStart = model::setAutoStart,
                            onForget = model::forgetDash,
                            onLock = model::setLock,
                        ),
                )
            }
        }
    }

    override fun onResume() {
        super.onResume()
        model.refresh()
    }

    @SuppressLint("BatteryLife") // the rider confirms in a system dialog; the reason is shown first
    private fun runStep(step: SetupStep) {
        when (step) {
            SetupStep.NEARBY -> {
                permissions.launch(arrayOf(Manifest.permission.BLUETOOTH_CONNECT))
            }

            SetupStep.NOTIFICATIONS -> {
                permissions.launch(arrayOf(Manifest.permission.POST_NOTIFICATIONS))
            }

            SetupStep.LOCATION -> {
                permissions.launch(
                    arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION),
                )
            }

            // Android 11+ sends the rider to the app's location page to pick "Allow all the time".
            SetupStep.BACKGROUND_LOCATION -> {
                permissions.launch(arrayOf(Manifest.permission.ACCESS_BACKGROUND_LOCATION))
            }

            SetupStep.BATTERY -> {
                systemScreen.launch(
                    Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, "package:$packageName".toUri()),
                )
            }

            SetupStep.PAIR -> {
                findDash()
            }

            SetupStep.MUSIC -> {
                systemScreen.launch(notificationAccess())
            }

            SetupStep.CALLS -> {
                permissions.launch(CallsFeature.PERMISSIONS)
            }
        }
    }

    private fun notificationAccess(): Intent =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Intent(Settings.ACTION_NOTIFICATION_LISTENER_DETAIL_SETTINGS).putExtra(
                Settings.EXTRA_NOTIFICATION_LISTENER_COMPONENT_NAME,
                ComponentName(this, DashNotificationListener::class.java).flattenToString(),
            )
        } else {
            Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)
        }

    private fun findDash() {
        DashAssociation(this).request(
            ContextCompat.getMainExecutor(this),
            onChooser = { chooser.launch(IntentSenderRequest.Builder(it).build()) },
            onError = { Toast.makeText(this, getString(R.string.pair_failed, it ?: ""), Toast.LENGTH_LONG).show() },
        )
    }
}
