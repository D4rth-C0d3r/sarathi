package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint
import org.xml.sax.Attributes
import org.xml.sax.helpers.DefaultHandler
import java.io.InputStream
import java.util.Locale
import javax.xml.XMLConstants
import javax.xml.parsers.SAXParserFactory

/**
 * Confirms a gap against current OSM data through Overpass (ADR-0012 §3), which is meant for
 * reads; the editing API isn't. One small query per gap: every way tagged `highway` (any value:
 * tracks, paths, construction, proposed), and car parks and fuel stations, near the gap.
 */
object Overpass {
    const val URL = "https://overpass-api.de/api/interpreter"

    /** How far around the gap to look: the gap rule's distance and a little more. */
    const val AROUND_M = 30

    /** The gap's line is thinned to a point every this many metres in the query. */
    private const val QUERY_STEP_M = 50.0
    private const val TIMEOUT_S = 25

    fun query(gap: Gap): String {
        val every = (QUERY_STEP_M / GapFinder.STEP_M).toInt().coerceAtLeast(1)
        val line =
            (gap.points.filterIndexed { i, _ -> i % every == 0 } + gap.points.last())
                .distinct()
                .joinToString(",") { String.format(Locale.ROOT, "%.6f,%.6f", it.lat, it.lng) }
        val around = "around:$AROUND_M,$line"
        return "[out:xml][timeout:$TIMEOUT_S];(" +
            "way[highway]($around);" +
            "way[amenity~\"^(parking|fuel)$\"]($around);" +
            ");out geom;"
    }

    /** Current OSM roads and car park or fuel areas, from Overpass's answer to [query]. */
    fun parse(xml: InputStream): RoadIndex {
        val handler = WayHandler()
        parser().parse(xml, handler)
        return RoadIndex.fromOsm(handler.roads, handler.areas)
    }

    /** A parser that never reads a DOCTYPE or external entities: the answer comes from the network. */
    private fun parser() =
        SAXParserFactory
            .newInstance()
            .apply {
                runCatching { setFeature(XMLConstants.FEATURE_SECURE_PROCESSING, true) }
                runCatching { setFeature("http://apache.org/xml/features/disallow-doctype-decl", true) }
                runCatching { setFeature("http://xml.org/sax/features/external-general-entities", false) }
                runCatching { setFeature("http://xml.org/sax/features/external-parameter-entities", false) }
            }.newSAXParser()

    private class WayHandler : DefaultHandler() {
        val roads = mutableListOf<List<GeoPoint>>()
        val areas = mutableListOf<List<GeoPoint>>()
        private var points: MutableList<GeoPoint>? = null
        private var highway = false
        private var area = false

        override fun startElement(
            uri: String?,
            localName: String?,
            qName: String,
            attributes: Attributes,
        ) {
            when (qName) {
                "way" -> {
                    points = mutableListOf()
                    highway = false
                    area = false
                }

                "nd" -> {
                    val lat = attributes.getValue("lat")?.toDoubleOrNull()
                    val lon = attributes.getValue("lon")?.toDoubleOrNull()
                    if (lat != null && lon != null) points?.add(GeoPoint(lat, lon))
                }

                "tag" -> {
                    val k = attributes.getValue("k")
                    val v = attributes.getValue("v")
                    if (k == "highway") highway = true
                    if (k == "amenity" && (v == "parking" || v == "fuel")) area = true
                }
            }
        }

        override fun endElement(
            uri: String?,
            localName: String?,
            qName: String,
        ) {
            if (qName != "way") return
            val p = points.orEmpty()
            if (highway && p.size >= 2) roads += p
            if (area && p.size >= MIN_RING && p.first() == p.last()) areas += p
            points = null
        }

        private companion object {
            const val MIN_RING = 4 // a closed way: three corners and the first again
        }
    }
}
