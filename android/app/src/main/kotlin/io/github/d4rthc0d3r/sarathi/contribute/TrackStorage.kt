package io.github.d4rthc0d3r.sarathi.contribute

import android.content.Context
import android.util.Log
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.RawGpsFix
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.TrackFile
import java.io.BufferedWriter
import java.io.File

/**
 * Ride tracks in app-private storage (ADR-0012), one file per dash connection. Never backed up
 * (backup is off for the app) and never shared without the rider choosing it for a gap.
 */
object TrackStorage {
    private const val TAG = "Contribute"
    private const val DIR = "osm-tracks"
    private const val M_PER_KM = 1000.0

    data class Summary(
        val rides: Int = 0,
        val km: Double = 0.0,
    )

    private fun dir(context: Context) = File(context.filesDir, DIR)

    private fun files(context: Context): List<File> =
        dir(context).listFiles()?.filter { TrackFile.startOf(it.name) != null }.orEmpty()

    /** A new track file started at [startUtcMs], with its header written. */
    fun newTrack(
        context: Context,
        startUtcMs: Long,
    ): BufferedWriter {
        val d = dir(context).apply { mkdirs() }
        return File(d, TrackFile.name(startUtcMs)).bufferedWriter().apply {
            write(TrackFile.HEADER)
            newLine()
        }
    }

    /** Finished tracks, newest first: not the one being recorded now. */
    fun finished(context: Context): List<File> =
        files(context).filter { it.name != TrackRecorder.activeName }.sortedByDescending { it.name }

    fun read(file: File): List<RawGpsFix> = file.useLines { TrackFile.read(it) }

    /** Deletes tracks older than TrackFile.KEEP_DAYS, and their gaps. */
    fun purgeExpired(context: Context) {
        val all = files(context)
        val old = TrackFile.expired(all.map { it.name }, System.currentTimeMillis()).toSet()
        all.filter { it.name in old }.forEach {
            it.delete()
            GapStore.deleteFor(context, it.name)
        }
        if (old.isNotEmpty()) Log.i(TAG, "deleted ${old.size} track(s) older than ${TrackFile.KEEP_DAYS} days")
    }

    fun deleteAll(context: Context) {
        val all = files(context)
        all.forEach { it.delete() }
        if (all.isNotEmpty()) Log.i(TAG, "deleted ${all.size} track(s)")
    }

    /** Rides and kilometres on the phone. Reads every file: call it off the main thread. */
    fun summary(context: Context): Summary {
        val tracks = files(context).map { f -> f.useLines { TrackFile.read(it) } }.filter { it.size > 1 }
        return Summary(tracks.size, tracks.sumOf { TrackFile.lengthM(it) } / M_PER_KM)
    }
}
