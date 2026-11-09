package io.github.d4rthc0d3r.sarathi

import android.content.Context
import android.util.Log
import androidx.compose.runtime.Composable
import androidx.lifecycle.viewmodel.compose.viewModel
import io.github.d4rthc0d3r.sarathi.feature.destination.DestinationViewModel
import io.github.d4rthc0d3r.sarathi.feature.destination.FilePlacesStore
import io.github.d4rthc0d3r.sarathi.feature.destination.RouteViewModel
import io.github.d4rthc0d3r.sarathi.feature.destination.SharedLinkResolver
import io.github.d4rthc0d3r.sarathi.maps.LastLocation
import io.github.d4rthc0d3r.sarathi.maps.MapsRegistry

/** The destination screen's view model: search, saved places and shared links. */
@Composable
internal fun destinationViewModel(context: Context): DestinationViewModel =
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

/** The route, its confirmation and the desk simulation. */
@Composable
internal fun routeViewModel(context: Context): RouteViewModel =
    viewModel {
        RouteViewModel(
            MapsRegistry.get(context).routing,
            { LastLocation.get(context) },
            RideStarter.deskEngine(context),
        )
    }
