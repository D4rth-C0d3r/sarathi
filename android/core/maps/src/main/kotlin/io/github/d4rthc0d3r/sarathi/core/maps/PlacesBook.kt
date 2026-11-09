package io.github.d4rthc0d3r.sarathi.core.maps

import kotlinx.serialization.Serializable

/** A place as kept on the phone, with when its coordinates were fetched. */
@Serializable
data class Kept(
    val place: Place,
    val fetchedAtMs: Long,
)

@Serializable
enum class Label { HOME, WORK, FAVOURITE }

@Serializable
data class SavedPlace(
    val label: Label,
    /** The rider's name for a favourite; Home and Work use their label. */
    val title: String,
    val kept: Kept,
)

/**
 * Recent destinations and saved places (ADR-0008). Google's terms allow keeping a place ID
 * indefinitely but its coordinates for at most 30 days: [expire] drops older Google coordinates,
 * and [needsLookup] says when a place must be looked up again before routing.
 */
@Serializable
data class PlacesBook(
    val recents: List<Kept> = emptyList(),
    val saved: List<SavedPlace> = emptyList(),
) {
    fun addRecent(
        place: Place,
        nowMs: Long,
    ): PlacesBook =
        copy(
            recents =
                (
                    listOf(Kept(place, nowMs)) +
                        recents.filterNot {
                            same(it.place, place)
                        }
                ).take(MAX_RECENTS),
        )

    fun removeRecent(place: Place): PlacesBook = copy(recents = recents.filterNot { same(it.place, place) })

    /** Saves [place]; Home and Work each hold one place, favourites are kept by title. */
    fun save(
        label: Label,
        title: String,
        place: Place,
        nowMs: Long,
    ): PlacesBook {
        val entry = SavedPlace(label, title.trim(), Kept(place, nowMs))
        val others =
            saved.filterNot {
                it.label == label && (label != Label.FAVOURITE || it.title.equals(entry.title, ignoreCase = true))
            }
        return copy(saved = (others + entry).sortedWith(compareBy({ it.label }, { it.title.lowercase() })))
    }

    fun remove(entry: SavedPlace): PlacesBook = copy(saved = saved - entry)

    /** After a lookup: the place with fresh coordinates, wherever it's kept. */
    fun refreshed(
        place: Place,
        nowMs: Long,
    ): PlacesBook {
        fun Kept.update() = if (same(this.place, place)) Kept(place, nowMs) else this
        return copy(recents = recents.map { it.update() }, saved = saved.map { it.copy(kept = it.kept.update()) })
    }

    /** Drops Google coordinates older than 30 days; the place ID stays. */
    fun expire(nowMs: Long): PlacesBook {
        fun Kept.expired() =
            if (place.source == PlaceSource.GOOGLE && place.at != null && nowMs - fetchedAtMs > GOOGLE_COORDS_MAX_MS) {
                copy(place = place.copy(at = null))
            } else {
                this
            }
        return copy(recents = recents.map { it.expired() }, saved = saved.map { it.copy(kept = it.kept.expired()) })
    }

    companion object {
        const val MAX_RECENTS = 20
        const val GOOGLE_COORDS_MAX_MS = 30L * 24 * 60 * 60 * 1000

        /** True when [place] has no coordinates but can be looked up again, by its ID or name. */
        fun needsLookup(place: Place) = place.at == null && (place.id != null || place.name.isNotBlank())

        /** The same place: same source and ID, or, without IDs, the same name and coordinates. */
        fun same(
            a: Place,
            b: Place,
        ): Boolean =
            if (a.id != null || b.id != null) {
                a.source == b.source && a.id == b.id
            } else {
                a.name == b.name && a.at == b.at
            }
    }
}
