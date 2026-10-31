package io.github.d4rthc0d3r.sarathi.feature.destination

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import io.github.d4rthc0d3r.sarathi.core.maps.Label
import io.github.d4rthc0d3r.sarathi.core.maps.PlaceSource
import io.github.d4rthc0d3r.sarathi.core.maps.SavedPlace
import io.github.d4rthc0d3r.sarathi.core.maps.SearchBackend
import io.github.d4rthc0d3r.sarathi.core.maps.Unavailable

/** The backend's name, which doubles as its attribution in search results. */
fun backendName(backend: SearchBackend): String =
    when (backend) {
        SearchBackend.GOOGLE -> "Google"
        SearchBackend.MAPPLS -> "Mappls"
        SearchBackend.OSM -> "Photon · © OpenStreetMap contributors"
    }

fun sourceName(source: PlaceSource): String =
    DestinationViewModel.backendOf(source)?.let(::backendName) ?: source.name.lowercase()

@Composable
fun reasonText(reason: Unavailable): String =
    stringResource(
        when (reason) {
            Unavailable.NO_KEY -> R.string.reason_no_key
            Unavailable.NO_TERMS -> R.string.reason_no_terms
            Unavailable.OVER_QUOTA -> R.string.reason_over_quota
            Unavailable.ACCOUNT -> R.string.reason_account
            Unavailable.OFFLINE -> R.string.reason_offline
            Unavailable.NO_DATA -> R.string.reason_no_data
            Unavailable.NO_ROUTE -> R.string.reason_no_route
            Unavailable.TIMEOUT -> R.string.reason_timeout
            Unavailable.ERROR -> R.string.reason_error
        },
    )

@Composable
fun savedTitle(saved: SavedPlace): String =
    when (saved.label) {
        Label.HOME -> stringResource(R.string.dest_home)
        Label.WORK -> stringResource(R.string.dest_work)
        Label.FAVOURITE -> saved.title
    }
