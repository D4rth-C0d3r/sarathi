package io.github.d4rthc0d3r.sarathi.contribute

import android.content.Context
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.DismissedAreas
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.GapFile
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.GapStatus
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.StoredGap
import java.io.File

/**
 * Gaps found in the rider's tracks (ADR-0012), one file per track, and the places dismissed for
 * good. App-private, never backed up; deleted with their track or when the helper is turned off.
 */
object GapStore {
    private const val DIR = "osm-gaps"
    private const val SUFFIX = ".gaps"
    private const val DISMISSED = "dismissed.txt"

    private fun dir(context: Context) = File(context.filesDir, DIR)

    private fun fileOf(
        context: Context,
        track: String,
    ) = File(dir(context), track + SUFFIX)

    /** Whether [track] was checked already (an empty file: checked, nothing found). */
    fun checked(
        context: Context,
        track: String,
    ): Boolean = fileOf(context, track).exists()

    @Synchronized
    fun save(
        context: Context,
        track: String,
        gaps: List<StoredGap>,
    ) {
        dir(context).mkdirs()
        fileOf(context, track).writeText(gaps.joinToString("") { GapFile.line(it) + "\n" })
    }

    @Synchronized
    fun all(context: Context): List<StoredGap> =
        dir(context)
            .listFiles { f -> f.name.endsWith(SUFFIX) }
            .orEmpty()
            .sortedByDescending { it.name }
            .flatMap { f -> f.useLines { GapFile.read(f.name.removeSuffix(SUFFIX), it) } }

    /** Sets [gap]'s status; dismissing also remembers the place for good. */
    @Synchronized
    fun update(
        context: Context,
        gap: StoredGap,
        status: GapStatus,
    ) {
        val file = fileOf(context, gap.track)
        if (!file.exists()) return
        val gaps = file.useLines { GapFile.read(gap.track, it) }
        save(context, gap.track, gaps.map { if (it.id == gap.id) it.copy(status = status) else it })
        if (status == GapStatus.DISMISSED) {
            val areas = dismissed(context).apply { dismiss(gap.gap) }
            File(dir(context), DISMISSED).writeText(areas.lines().joinToString("") { it + "\n" })
        }
    }

    @Synchronized
    fun dismissed(context: Context): DismissedAreas {
        val f = File(dir(context), DISMISSED)
        return if (f.exists()) f.useLines { DismissedAreas.read(it) } else DismissedAreas()
    }

    /** Replaces [old] with what checking it again found (none, or parts of it). */
    @Synchronized
    fun replace(
        context: Context,
        old: StoredGap,
        now: List<StoredGap>,
    ) {
        val file = fileOf(context, old.track)
        if (!file.exists()) return
        val gaps = file.useLines { GapFile.read(old.track, it) }
        save(context, old.track, gaps.flatMap { if (it.id == old.id) now else listOf(it) })
    }

    fun deleteFor(
        context: Context,
        track: String,
    ) {
        fileOf(context, track).delete()
    }

    fun deleteAll(context: Context) {
        dir(context).deleteRecursively()
    }
}
