package io.github.d4rthc0d3r.sarathi.core.mapdata

import io.github.d4rthc0d3r.sarathi.core.protocol.Provenance

/**
 * How one source's tiles map to chunk roads (classes 0 motorway ... 6 service) and areas (water,
 * parks), as tools/make_chunks.py's schemas do. The provenance goes with every chunk (ADR-0009).
 */
sealed class MapSchema(
    val layers: Set<String>,
    val roadLayer: String,
    val provenance: Provenance,
) {
    abstract fun roadClass(tags: Map<String, Any?>): Int?

    /** The polygons to keep, as (kind, feature): water first, then parks. */
    abstract fun areas(tile: Map<String, List<TileFeature>>): List<Pair<Int, TileFeature>>

    /** OpenFreeMap (online): transportation by class; water; park. */
    data object OpenMapTiles : MapSchema(
        setOf("transportation", "water", "park"),
        "transportation",
        Provenance.OSM_OPENFREEMAP,
    ) {
        override fun roadClass(tags: Map<String, Any?>): Int? =
            ChunkMaker.ROAD_CLASSES.indexOf(tags["class"] as? String).takeIf { it >= 0 }

        override fun areas(tile: Map<String, List<TileFeature>>) =
            tile["water"].orEmpty().map { WATER to it } + tile["park"].orEmpty().map { PARK to it }
    }

    /** Protomaps basemap v4 (the offline region): roads by kind_detail, water, landuse parks. */
    data object Protomaps : MapSchema(
        setOf("roads", "water", "landuse"),
        "roads",
        Provenance.OSM_PROTOMAPS_OFFLINE,
    ) {
        // kind_detail is the OSM highway tag.
        private val roads =
            mapOf(
                "motorway" to MOTORWAY,
                "motorway_link" to MOTORWAY,
                "trunk" to TRUNK,
                "trunk_link" to TRUNK,
                "primary" to PRIMARY,
                "primary_link" to PRIMARY,
                "secondary" to SECONDARY,
                "secondary_link" to SECONDARY,
                "tertiary" to TERTIARY,
                "tertiary_link" to TERTIARY,
                "residential" to MINOR,
                "unclassified" to MINOR,
                "living_street" to MINOR,
                "road" to MINOR,
                "service" to SERVICE,
            )
        private val parks = setOf("park", "national_park", "nature_reserve", "protected_area")
        private val notWater = setOf("swimming_pool")

        override fun roadClass(tags: Map<String, Any?>): Int? = roads[tags["kind_detail"] as? String]

        override fun areas(tile: Map<String, List<TileFeature>>) =
            tile["water"].orEmpty().filter { it.tags["kind"] !in notWater }.map { WATER to it } +
                tile["landuse"].orEmpty().filter { it.tags["kind"] in parks }.map { PARK to it }
    }

    companion object {
        const val WATER = 0
        const val PARK = 1

        // Road classes, as in the protocol's chunk lines.
        const val MOTORWAY = 0
        const val TRUNK = 1
        const val PRIMARY = 2
        const val SECONDARY = 3
        const val TERTIARY = 4
        const val MINOR = 5
        const val SERVICE = 6
    }
}
