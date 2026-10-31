package io.github.d4rthc0d3r.sarathi.core.protocol

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * JSON messages against docs/protocol/vectors/json_messages.json: every valid vector decodes
 * (from either sender) and re-encodes to its canonical bytes; every invalid one is dropped for
 * the vector's reason.
 */
class JsonMessagesTest {
    private val vectors = Vectors.load("json_messages.json")

    private fun sender(v: JsonObject) = Sender.entries.single { it.wire == v["direction"].str() }

    @Test
    fun validVectorsRoundTripToCanonical() {
        val cases = vectors["valid"].list().map { it.obj() }
        assertTrue(cases.size >= 20)
        for (v in cases) {
            val m = JsonCodec.decode(v["type"].int(), v["payload"].str().toByteArray(), sender(v))
            assertEquals(v["name"].str(), v["canonical"].str(), String(JsonCodec.encode(m), Charsets.UTF_8))
        }
    }

    @Test
    fun invalidVectorsAreDroppedForTheirReason() {
        val cases = vectors["invalid"].list().map { it.obj() }
        assertTrue(cases.size >= 10)
        for (v in cases) {
            val e =
                assertThrows(v["name"].str(), ProtocolException::class.java) {
                    JsonCodec.decode(v["type"].int(), v["payload"].str().toByteArray(), sender(v))
                }
            assertEquals(v["name"].str(), v["error"].str(), e.reason)
        }
    }

    @Test
    fun decodedValues() {
        val nav =
            JsonCodec.decode(
                MsgType.NAV,
                (
                    """{"t":"nav","m":"teleport","exit":3,"dist_m":1,"street":"Gä","rem_s":1,"rem_m":1,""" +
                        """"flags":["arrived","rerouting","arrived"]}"""
                ).toByteArray(),
                Sender.PHONE,
            ) as Nav
        assertEquals(Maneuver.STRAIGHT, nav.maneuver)
        assertEquals("Gä", nav.street)
        assertEquals(setOf(NavFlag.REROUTING, NavFlag.ARRIVED), nav.flags)
        val hello =
            JsonCodec.decode(
                MsgType.HELLO,
                (
                    """{"t":"hello","proto":[1,0],"fw":"0.5.0","seq":500,""" +
                        """"caps":{"styles":[],"chunk_kb":1,"max_msg":15360},"route":null,"epoch":null}"""
                ).toByteArray(),
                Sender.DEVICE,
            ) as HelloDevice
        assertEquals(null, hello.route)
    }

    @Test
    fun decodeRejects() {
        fun reason(
            type: Int,
            json: String,
            sender: Sender = Sender.PHONE,
        ) = assertThrows(ProtocolException::class.java) { JsonCodec.decode(type, json.toByteArray(), sender) }.reason
        assertEquals("unknown_type", reason(MsgType.FLOW, """{"t":"flow"}"""))
        assertEquals("unknown_type", reason(MsgType.NAV, """{"t":"nav"}""", Sender.DEVICE))
        assertEquals("invalid", reason(MsgType.EVENT, """{"t":"event","kind":"reroute","percent":101}"""))
        assertEquals("invalid", reason(MsgType.EVENT, """{"t":"event","kind":"cost_warning","backend":"google"}"""))
        assertEquals("invalid", reason(MsgType.HELLO, """{"t":"hello","proto":[1,0,0],"app":"x","caps":[]}"""))
        assertEquals("invalid", reason(MsgType.PHONE_BATT, """{"t":"phone_batt","level":7.0,"charging":true}"""))
        assertEquals("invalid", reason(MsgType.CALL, """{"t":"call","phase":"idle","caller":"a\u0000b"}"""))
        assertEquals("invalid", reason(MsgType.CALL, """{"t":"call","phase":"idle","caller":"\ud83d"}"""))
        assertEquals("invalid", reason(MsgType.FRAME, """{"t":"frame","epoch":1,"epoch":2}"""))
        assertEquals("type_mismatch", reason(MsgType.FRAME, """{"epoch":1}"""))
        assertEquals("not_json", reason(MsgType.FRAME, """{"t":"frame","epoch":01}"""))
    }

    @Test
    fun encodeChecksLimits() {
        assertThrows(
            IllegalArgumentException::class.java,
        ) { Call(CallPhase.RINGING, "x".repeat(Call.CALLER_BYTES + 1)) }
        assertThrows(IllegalArgumentException::class.java) { Event(EventKind.FALLBACK, from = Routing.GOOGLE) }
        assertThrows(IllegalArgumentException::class.java) { TimeSync("2026-1-2T03:04:05", 0) }
        // 64 entries of the largest keys exceed 2,048 bytes: the device batches.
        val big = ChunkKey(1, U32_MAX, U32_MAX, U32_MAX)
        assertThrows(IllegalArgumentException::class.java) { JsonCodec.encode(ChunkAck(List(64) { big }, emptyList())) }
        assertEquals(
            """{"t":"event","kind":"cost_warning","backend":"mappls","percent":90}""",
            String(JsonCodec.encode(Event(EventKind.COST_WARNING, backend = Search.MAPPLS, percent = 90))),
        )
    }

    @Test
    fun maneuverNames() {
        for (m in Maneuver.entries) assertEquals(m, Maneuver.fromName(m.wire))
        assertEquals(19, Maneuver.FERRY.code)
    }
}
