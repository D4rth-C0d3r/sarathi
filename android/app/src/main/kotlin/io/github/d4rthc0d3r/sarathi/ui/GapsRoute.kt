package io.github.d4rthc0d3r.sarathi.ui

import android.content.Context
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import io.github.d4rthc0d3r.sarathi.BuildConfig
import io.github.d4rthc0d3r.sarathi.R
import io.github.d4rthc0d3r.sarathi.contribute.GapActions
import io.github.d4rthc0d3r.sarathi.contribute.GapCheck
import io.github.d4rthc0d3r.sarathi.contribute.GapStore
import io.github.d4rthc0d3r.sarathi.contribute.OsmAccount
import io.github.d4rthc0d3r.sarathi.contribute.OsmApi
import io.github.d4rthc0d3r.sarathi.contribute.Sent
import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.GapStatus
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.StoredGap
import io.github.d4rthc0d3r.sarathi.link.ActiveRide
import io.github.d4rthc0d3r.sarathi.maps.MapsRegistry
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** Missing roads on recent rides (ADR-0012): checked when the rider asks, never during a ride. */
@Composable
fun GapsRoute(onBack: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val plan = ActiveRide.plan.collectAsStateWithLifecycle()
    val signedIn = OsmAccount.signedIn(context).collectAsStateWithLifecycle()
    var tick by remember { mutableIntStateOf(0) }
    var checking by remember { mutableStateOf<String?>(null) }
    var message by remember { mutableStateOf<String?>(null) }
    val stored by produceState(emptyList<StoredGap>(), tick) {
        value = withContext(Dispatchers.IO) { GapStore.all(context) }
    }
    val state =
        GapsState(
            context,
            scope,
            onChecking = { checking = it },
            onMessage = { message = it },
            onChanged = { tick++ },
        )
    GapsScreen(
        GapsUi(
            open = stored.filter { it.status == GapStatus.OPEN },
            unchecked = stored.count { it.status == GapStatus.UNCHECKED },
            riding = plan.value != null,
            checking = checking,
            signedIn = signedIn.value,
            canSignIn = OsmAccount.configured,
            testServer = !BuildConfig.OSM_LIVE,
            sent = stored.filter { it.status in setOf(GapStatus.TRACE_SENT, GapStatus.NOTE_SENT, GapStatus.EDITED) },
            message = message,
        ),
        state.actions(onBack),
    )
}

/** What the missing-roads screen's buttons do; each reports back through the callbacks. */
private class GapsState(
    private val context: Context,
    private val scope: CoroutineScope,
    private val onChecking: (String?) -> Unit,
    private val onMessage: (String) -> Unit,
    private val onChanged: () -> Unit,
) {
    fun actions(onBack: () -> Unit) =
        GapsActions(
            onBack = onBack,
            onCheck = ::check,
            onSkip = { setStatus(it, GapStatus.SKIPPED) },
            onDismiss = { setStatus(it, GapStatus.DISMISSED) },
            roads = { GapCheck.roadsNear(context, it) },
            onSignIn = { OsmAccount.startLogin(context) },
            onSignOut = { scope.launch { OsmAccount.signOut(context, MapsRegistry.get(context).http) } },
            tracePoints = { g -> GapActions.tracePoints(context, g).map { GeoPoint(it.lat, it.lon) } },
            onSendTrace = { g, description, visibility ->
                send { GapActions.sendTrace(context, g, description, visibility) }
            },
            onSendNote = { g, text -> send { GapActions.sendNote(context, g, text) } },
            prepareEdit = { g -> GapActions.prepareEdit(context, g) },
            onSendEdit = { g, plan, road, comment -> send { GapActions.sendEdit(context, g, plan, road, comment) } },
        )

    private fun check() {
        onChecking(context.getString(R.string.gaps_checking_ride))
        scope.launch {
            GapCheck.run(context) { onChecking(it) }
            onChecking(null)
            onChanged()
        }
    }

    private fun setStatus(
        gap: StoredGap,
        status: GapStatus,
    ) {
        scope.launch {
            withContext(Dispatchers.IO) { GapStore.update(context, gap, status) }
            onChanged()
        }
    }

    private fun send(call: suspend () -> Sent) {
        scope.launch {
            onMessage(text(call()))
            onChanged()
        }
    }

    private fun text(sent: Sent): String =
        when (sent) {
            is Sent.Ok -> {
                context.getString(R.string.gaps_sent_ok)
            }

            Sent.SignedOut -> {
                context.getString(R.string.gaps_sent_signed_out)
            }

            is Sent.Failed -> {
                when (sent.why) {
                    GapActions.LIMIT_REACHED -> context.getString(R.string.gaps_limit)
                    OsmApi.CHANGED_MEANWHILE -> context.getString(R.string.edit_conflict)
                    else -> context.getString(R.string.gaps_sent_failed, sent.why)
                }
            }
        }
}
