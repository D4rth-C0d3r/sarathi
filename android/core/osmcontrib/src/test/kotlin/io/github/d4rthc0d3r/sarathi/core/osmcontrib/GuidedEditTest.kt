package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import io.github.d4rthc0d3r.sarathi.core.mapdata.GeoPoint
import io.github.d4rthc0d3r.sarathi.core.osmcontrib.TestMaps.at
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.Locale

class GuidedEditTest {
    // An OSM road along y = 0, with nodes at the given metres east.
    private fun osm(
        nodesAtM: List<Double>,
        highway: String = "residential",
    ): OsmData {
        val nodes =
            nodesAtM.mapIndexed {
                i,
                e,
                ->
                at(0.0, e).let { p ->
                    "<node id=\"${i + 1}\" version=\"3\" lat=\"${f(p.lat)}\" lon=\"${f(p.lng)}\"/>"
                }
            }
        val nds = nodesAtM.indices.joinToString("") { "<nd ref=\"${it + 1}\"/>" }
        val xml =
            "<osm>${nodes.joinToString("")}<way id=\"77\" version=\"5\">$nds" +
                "<tag k=\"highway\" v=\"$highway\"/><tag k=\"name\" v=\"Old &amp; road\"/></way></osm>"
        return OsmData.parse(xml.byteInputStream())
    }

    private fun f(d: Double) = String.format(Locale.ROOT, "%.7f", d)

    // The rider left the road at 600 m east, rode 100 m north of it, came back at 1,200 m.
    private fun gap(endNorth: Double = 35.0) =
        Gap(
            listOf(at(35.0, 600.0), at(100.0, 700.0), at(100.0, 900.0), at(100.0, 1100.0), at(endNorth, 1200.0)),
            700.0,
            1,
            2,
        )

    @Test
    fun `ends join existing nodes when there are some`() {
        val plan = (GuidedEdit.plan(gap(), osm(listOf(0.0, 600.0, 1200.0, 2000.0))) as GuidedEdit.Result.Ok).plan
        assertEquals(2L, (plan.start as Join.Existing).node.id)
        assertEquals(3L, (plan.end as Join.Existing).node.id)
        val change = OsmChange.build(plan, RoadType.TRACK, 42)
        assertTrue(change.contains("<tag k=\"highway\" v=\"track\"/>"))
        assertTrue(change.contains("<tag k=\"fixme\" v=\"${OsmChange.FIXME}\"/>"))
        assertTrue(!change.contains("<modify>")) // nothing existing changes
        assertTrue(change.contains("<nd ref=\"2\"/>") && change.contains("<nd ref=\"3\"/>"))
        assertEquals(plan.middle.size, Regex("<node id=\"-").findAll(change).count())
    }

    @Test
    fun `ends join new nodes on the road, in order along it`() {
        val plan = (GuidedEdit.plan(gap(), osm(listOf(0.0, 2000.0))) as GuidedEdit.Result.Ok).plan
        assertTrue(plan.start is Join.Inserted && plan.end is Join.Inserted)
        val change = OsmChange.build(plan, RoadType.RESIDENTIAL, 42)
        // The existing way keeps its nodes, version and tags, with the two new nodes between.
        val modified = change.substringAfter("<modify>").substringBefore("</modify>")
        assertTrue(modified, modified.startsWith("<way id=\"77\" changeset=\"42\" version=\"5\">"))
        val refs = Regex("<nd ref=\"(-?\\d+)\"/>").findAll(modified).map { it.groupValues[1].toLong() }.toList()
        assertEquals(4, refs.size)
        assertEquals(1L, refs.first())
        assertEquals(2L, refs.last())
        assertEquals(-1L, refs[1]) // the start's node, at 600 m, first along the way
        assertTrue(modified.contains("<tag k=\"name\" v=\"Old &amp; road\"/>"))
        assertTrue(modified.contains("<tag k=\"highway\" v=\"residential\"/>"))
    }

    @Test
    fun `an end away from every road is refused`() {
        val r = GuidedEdit.plan(gap(endNorth = 80.0), osm(listOf(0.0, 600.0, 1200.0, 2000.0)))
        assertEquals(NoEdit.END_NOT_NEAR_A_ROAD, (r as GuidedEdit.Result.Refused).why)
    }

    @Test
    fun `planned roads aren't joined`() {
        val r = GuidedEdit.plan(gap(), osm(listOf(0.0, 2000.0), highway = "proposed"))
        assertEquals(NoEdit.START_NOT_NEAR_A_ROAD, (r as GuidedEdit.Result.Refused).why)
    }

    @Test
    fun `too long is refused`() {
        val long = Gap(listOf(at(35.0, 0.0), at(3000.0, 3000.0), at(35.0, 6000.0)), 8000.0, 1, 2)
        val r = GuidedEdit.plan(long, osm(listOf(0.0, 6000.0)))
        assertEquals(NoEdit.TOO_LONG, (r as GuidedEdit.Result.Refused).why)
    }

    @Test
    fun `simplify keeps the shape and drops the rest`() {
        val straight = (0..20).map { at(0.0, it * 10.0) }
        assertEquals(2, GuidedEdit.simplify(straight, 5.0).size)
        val bend: List<GeoPoint> = straight + (1..20).map { at(it * 10.0, 200.0) }
        assertEquals(3, GuidedEdit.simplify(bend, 5.0).size)
    }

    @Test
    fun `the changeset says what it is`() {
        val cs = OsmChange.changeset("Add a lane \"by the temple\"", "sarathi 0.0.1")
        assertTrue(cs.contains("<tag k=\"comment\" v=\"Add a lane &quot;by the temple&quot;\"/>"))
        assertTrue(cs.contains("<tag k=\"created_by\" v=\"sarathi 0.0.1\"/>"))
        assertTrue(cs.contains("<tag k=\"source\" v=\"survey;gps\"/>"))
        assertTrue(cs.contains("<tag k=\"review_requested\" v=\"yes\"/>"))
    }
}
