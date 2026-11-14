package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import java.time.Instant
import java.time.temporal.ChronoUnit
import java.util.Locale

/** The rider's GPS trace as GPX 1.1, for OSM's trace upload (ADR-0012 §4): track points with times. */
object Gpx {
    fun write(
        fixes: List<RawGpsFix>,
        creator: String,
    ): String =
        buildString {
            append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n")
            append("<gpx version=\"1.1\" creator=\"").append(escape(creator)).append("\" ")
            append("xmlns=\"http://www.topografix.com/GPX/1/1\">\n<trk><trkseg>\n")
            for (f in fixes) {
                append(String.format(Locale.ROOT, "<trkpt lat=\"%.6f\" lon=\"%.6f\">", f.lat, f.lon))
                append(
                    "<time>",
                ).append(Instant.ofEpochMilli(f.timeUtcMs).truncatedTo(ChronoUnit.SECONDS)).append("</time>")
                append("</trkpt>\n")
            }
            append("</trkseg></trk>\n</gpx>\n")
        }

    private fun escape(s: String) =
        s
            .replace("&", "&amp;")
            .replace("<", "&lt;")
            .replace(">", "&gt;")
            .replace("\"", "&quot;")
}
