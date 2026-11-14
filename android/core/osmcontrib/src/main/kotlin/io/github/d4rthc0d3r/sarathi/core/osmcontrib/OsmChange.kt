package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint
import java.util.Locale

/** The XML a guided edit sends to OSM's editing API (ADR-0012 §4, §5). */
object OsmChange {
    /** Good practice: a fixme on a position from a single GPS trace. */
    const val FIXME = "position from one GPS trace; check against imagery"

    /** The changeset's tags: the rider's comment, and what made it and from what. */
    fun changeset(
        comment: String,
        createdBy: String,
    ): String =
        buildString {
            append("<osm><changeset>")
            tag("comment", comment)
            tag("created_by", createdBy)
            tag("source", "survey;gps")
            tag("review_requested", "yes")
            append("</changeset></osm>")
        }

    /**
     * The new road as osmChange: its new nodes and way, and each existing road an end joins with
     * the new node added (every tag and node it had kept, at its version).
     */
    fun build(
        plan: EditPlan,
        road: RoadType,
        changeset: Long,
    ): String {
        var next = -1L
        val created = mutableListOf<Pair<Long, GeoPoint>>()

        fun newNode(at: GeoPoint): Long = next--.also { created += it to at }
        val inserted = mutableMapOf<Long, MutableList<Pair<Join.Inserted, Long>>>()

        fun ref(join: Join): Long =
            when (join) {
                is Join.Existing -> {
                    join.node.id
                }

                is Join.Inserted -> {
                    newNode(join.at).also {
                        inserted.getOrPut(join.way.id) { mutableListOf() } +=
                            join to it
                    }
                }
            }
        val wayRefs = listOf(ref(plan.start)) + plan.middle.map(::newNode) + listOf(ref(plan.end))
        val wayId = next--
        return buildString {
            append("<osmChange version=\"0.6\" generator=\"sarathi\"><create>")
            for ((id, at) in created) {
                append(
                    String.format(
                        Locale.ROOT,
                        "<node id=\"%d\" changeset=\"%d\" lat=\"%.7f\" lon=\"%.7f\"/>",
                        id,
                        changeset,
                        at.lat,
                        at.lng,
                    ),
                )
            }
            append("<way id=\"$wayId\" changeset=\"$changeset\">")
            wayRefs.forEach { append("<nd ref=\"$it\"/>") }
            tag("highway", road.highway)
            tag("fixme", FIXME)
            append("</way></create>")
            if (inserted.isNotEmpty()) {
                append("<modify>")
                for ((_, joins) in inserted) modified(joins, changeset)
                append("</modify>")
            }
            append("</osmChange>")
        }
    }

    // An existing way with the new node(s) added after the node each join follows; two joins on
    // the same segment go in order along it.
    private fun StringBuilder.modified(
        joins: List<Pair<Join.Inserted, Long>>,
        changeset: Long,
    ) {
        val way = joins.first().first.way
        val after = joins.groupBy { it.first.index }
        append("<way id=\"${way.id}\" changeset=\"$changeset\" version=\"${way.version}\">")
        way.nodes.forEachIndexed { i, id ->
            append("<nd ref=\"$id\"/>")
            after[i]
                .orEmpty()
                .sortedBy { (j, _) -> RoadIndex.segmentDistanceM(j.at, j.from, j.from) }
                .forEach { (_, newId) -> append("<nd ref=\"$newId\"/>") }
        }
        way.tags.forEach { (k, v) -> tag(k, v) }
        append("</way>")
    }

    private fun StringBuilder.tag(
        k: String,
        v: String,
    ) {
        append("<tag k=\"")
            .append(escape(k))
            .append("\" v=\"")
            .append(escape(v))
            .append("\"/>")
    }

    private fun escape(s: String) =
        s
            .replace("&", "&amp;")
            .replace("<", "&lt;")
            .replace(">", "&gt;")
            .replace("\"", "&quot;")
            .replace("\n", "&#10;")
}
