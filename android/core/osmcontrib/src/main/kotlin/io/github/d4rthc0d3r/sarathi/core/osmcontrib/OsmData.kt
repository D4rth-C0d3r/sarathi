package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint
import org.xml.sax.Attributes
import org.xml.sax.helpers.DefaultHandler
import java.io.InputStream

/**
 * Current OSM data for a small area, from the editing API's `map` call, for a guided edit
 * (ADR-0012 §4): the nodes and the roads (ways tagged `highway`), with their versions.
 */
class OsmData(
    val nodes: Map<Long, Node>,
    val ways: List<Way>,
) {
    data class Node(
        val id: Long,
        val at: GeoPoint,
        val version: Int,
    )

    data class Way(
        val id: Long,
        val version: Int,
        val nodes: List<Long>,
        val tags: Map<String, String>,
    ) {
        /** A road the new one can join: not planned, not being built, not an area. */
        val joinable: Boolean
            get() =
                tags["highway"] != null &&
                    tags["highway"] !in NOT_ROADS &&
                    tags["area"] != "yes" &&
                    nodes.size >= 2
    }

    /** A way's positions, skipping any node missing from the download. */
    fun line(way: Way): List<GeoPoint> = way.nodes.mapNotNull { nodes[it]?.at }

    companion object {
        private val NOT_ROADS = setOf("proposed", "construction", "abandoned", "platform", "razed")

        fun parse(xml: InputStream): OsmData {
            val handler = Handler()
            Overpass.parser().parse(xml, handler)
            return OsmData(handler.nodes, handler.ways)
        }
    }

    private class Handler : DefaultHandler() {
        val nodes = HashMap<Long, Node>()
        val ways = mutableListOf<Way>()
        private var way: Pair<Long, Int>? = null
        private val refs = mutableListOf<Long>()
        private val tags = HashMap<String, String>()

        override fun startElement(
            uri: String?,
            localName: String?,
            qName: String,
            a: Attributes,
        ) {
            when (qName) {
                "node" -> {
                    node(a)?.let { nodes[it.id] = it }
                }

                "way" -> {
                    way =
                        a.getValue("id")?.toLongOrNull()?.let { id ->
                            a.getValue("version")?.toIntOrNull()?.let {
                                id to
                                    it
                            }
                        }
                    refs.clear()
                    tags.clear()
                }

                "nd" -> {
                    a.getValue("ref")?.toLongOrNull()?.let { if (way != null) refs += it }
                }

                "tag" -> {
                    if (way != null) tag(a)?.let { (k, v) -> tags[k] = v }
                }
            }
        }

        private fun node(a: Attributes): Node? {
            val id = a.getValue("id")?.toLongOrNull()
            val at =
                a.getValue("lat")?.toDoubleOrNull()?.let { lat ->
                    a.getValue("lon")?.toDoubleOrNull()?.let { GeoPoint(lat, it) }
                }
            val v = a.getValue("version")?.toIntOrNull()
            return if (id != null && at != null && v != null) Node(id, at, v) else null
        }

        private fun tag(a: Attributes): Pair<String, String>? =
            a.getValue("k")?.let { k ->
                a.getValue("v")?.let {
                    k to
                        it
                }
            }

        override fun endElement(
            uri: String?,
            localName: String?,
            qName: String,
        ) {
            if (qName != "way") return
            way?.let { (id, v) -> ways += Way(id, v, refs.toList(), tags.toMap()) }
            way = null
        }
    }
}
