package io.github.d4rthc0d3r.sarathi

import android.Manifest
import android.annotation.SuppressLint
import android.content.ComponentName
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.util.Log
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.core.content.ContextCompat
import androidx.core.net.toUri
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import io.github.d4rthc0d3r.sarathi.core.maps.Meter
import io.github.d4rthc0d3r.sarathi.core.maps.RoutingBackend
import io.github.d4rthc0d3r.sarathi.feature.calls.CallsFeature
import io.github.d4rthc0d3r.sarathi.feature.destination.DestinationActions
import io.github.d4rthc0d3r.sarathi.feature.destination.DestinationScreen
import io.github.d4rthc0d3r.sarathi.feature.destination.DestinationViewModel
import io.github.d4rthc0d3r.sarathi.feature.destination.FilePlacesStore
import io.github.d4rthc0d3r.sarathi.feature.destination.RouteViewModel
import io.github.d4rthc0d3r.sarathi.feature.destination.SharedLinkResolver
import io.github.d4rthc0d3r.sarathi.feature.navigation.FerrostarGuidance
import io.github.d4rthc0d3r.sarathi.link.DashAssociation
import io.github.d4rthc0d3r.sarathi.link.DashNotificationListener
import io.github.d4rthc0d3r.sarathi.maps.Keys
import io.github.d4rthc0d3r.sarathi.maps.LastLocation
import io.github.d4rthc0d3r.sarathi.maps.MapsRegistry
import io.github.d4rthc0d3r.sarathi.maps.MapsStore
import io.github.d4rthc0d3r.sarathi.maps.google.GoogleNavRouting
import io.github.d4rthc0d3r.sarathi.ui.HomeActions
import io.github.d4rthc0d3r.sarathi.ui.HomeScreen
import io.github.d4rthc0d3r.sarathi.ui.MainViewModel
import io.github.d4rthc0d3r.sarathi.ui.MapsSettingsActions
import io.github.d4rthc0d3r.sarathi.ui.MapsSettingsScreen
import io.github.d4rthc0d3r.sarathi.ui.MapsStatus
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

    /** Changes when Google's terms dialog closes, so the settings screen reads them again. */
    private var termsTick by mutableIntStateOf(0)

    /** Text or a link from another app, waiting for the destination screen (ADR-0008). */
    private var shared by mutableStateOf<String?>(null)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (savedInstanceState == null) shared = sharedText(intent)
        enableEdgeToEdge()
        setContent {
            SarathiTheme {
                var screen by rememberSaveable { mutableStateOf(Screen.HOME) }
                if (shared != null) screen = Screen.DESTINATION
                if (screen != Screen.HOME) {
                    val home = { screen = Screen.HOME }
                    BackHandler(onBack = home)
                    when (screen) {
                        Screen.MAPS -> MapsSettings(onBack = home)
                        else -> Destination(onBack = home)
                    }
                    return@SarathiTheme
                }
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
                            onMapsSettings = { screen = Screen.MAPS },
                            onWhereTo = { screen = Screen.DESTINATION },
                        ),
                )
            }
        }
    }

    @Composable
    private fun MapsSettings(onBack: () -> Unit) {
        val store = MapsStore.get(this)
        val settings = store.settings.collectAsStateWithLifecycle()
        val usage = store.usage.collectAsStateWithLifecycle()
        val status =
            MapsStatus(
                googleKey = Keys.google != null,
                googleTerms = termsTick.let { GoogleNavRouting.termsAccepted(application) },
                graphHopperKey = Keys.graphHopper != null,
                mapplsConfigured = Keys.mapplsConfigured,
                meters = usage.value.let { Meter.entries.map { store.guard.status(it) } },
            )
        MapsSettingsScreen(
            settings.value,
            status,
            MapsSettingsActions(store::update, onBack) { GoogleNavRouting.askForTerms(this) { termsTick++ } },
        )
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        sharedText(intent)?.let { shared = it }
    }

    private fun sharedText(intent: Intent?): String? =
        when (intent?.action) {
            // Some apps put the place's name in the subject and only the link in the text.
            Intent.ACTION_SEND -> {
                listOfNotNull(intent.getStringExtra(Intent.EXTRA_SUBJECT), intent.getStringExtra(Intent.EXTRA_TEXT))
                    .joinToString("\n")
                    .ifBlank { null }
            }

            Intent.ACTION_VIEW -> {
                intent.dataString
            }

            else -> {
                null
            }
        }

    @Composable
    private fun Destination(onBack: () -> Unit) {
        val context = applicationContext
        val model =
            viewModel {
                val maps = MapsRegistry.get(context)
                val near = { LastLocation.get(context) }
                DestinationViewModel(
                    maps.search,
                    FilePlacesStore(context),
                    SharedLinkResolver(maps.search, near, maps::redirect) { Log.i("Links", it) },
                    near,
                )
            }
        LaunchedEffect(shared) {
            shared?.let {
                model.onShared(it)
                shared = null
            }
        }
        val routeModel =
            viewModel {
                RouteViewModel(MapsRegistry.get(context).routing, { LastLocation.get(context) }) { route, request ->
                    // Google's own guidance comes with step F6.3.
                    if (route.backend == RoutingBackend.GOOGLE) null else FerrostarGuidance(request)
                }
            }
        val route = routeModel.ui.collectAsStateWithLifecycle()
        val ui = model.ui.collectAsStateWithLifecycle()
        LaunchedEffect(ui.value.chosen, ui.value.trip) { routeModel.clear() }
        DestinationScreen(
            ui.value,
            DestinationActions(
                onQuery = model::onQuery,
                onSuggestion = model::onSuggestion,
                onKept = model::onKept,
                onSave = model::save,
                onRemoveRecent = { place -> model.edit { it.removeRecent(place) } },
                onRemoveSaved = { saved -> model.edit { it.remove(saved) } },
                onClearChosen = {
                    model.clearChosen()
                    routeModel.clear()
                },
                onRoute = routeModel::route,
                onSimulate = routeModel::simulate,
                onStopSimulation = routeModel::stopSimulation,
                onBack = onBack,
            ),
            route = route.value,
        )
    }

    private enum class Screen { HOME, MAPS, DESTINATION }

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
