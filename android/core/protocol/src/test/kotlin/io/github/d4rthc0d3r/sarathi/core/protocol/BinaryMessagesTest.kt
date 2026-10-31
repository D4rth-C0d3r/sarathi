package io.github.d4rthc0d3r.sarathi.core.protocol

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Binary messages against docs/protocol/vectors/binary_messages.json: each valid vector's
 * `decoded` content encodes to its bytes and the bytes decode back to it; each invalid one is
 * dropped for the vector's reason.
 */
class BinaryMessagesTest {
    private val vectors = Vectors.load("binary_messages.json")

    private fun points(v: JsonValue?) = v.list().map { Point(it.list()[0].int(), it.list()[1].int()) }

    private fun key(v: JsonValue) = v.list().let { ChunkKey(it[0].int(), it[1].long(), it[2].long(), it[3].long()) }

    private fun shapes(v: JsonValue?) = v.list().map { Shape(it.list()[0].int(), points(it.list()[1])) }

    private fun expected(
        type: Int,
        d: JsonObject,
    ): BinaryMessage =
        when (type) {
            MsgType.POSE -> {
                Pose(
                    d["epoch"].int(),
                    d["t_ms"].long(),
                    d["x_dm"].int(),
                    d["y_dm"].int(),
                    d["heading_cdeg"].int(),
                    d["speed_cms"].int(),
                    d["acc_m"].int(),
                )
            }

            MsgType.ROUTE -> {
                Route(
                    d["route_id"].long(),
                    d["version"].int(),
                    Routing.entries[d["backend"].int()],
                    d["epoch"].int(),
                    d["mode"].int(),
                    d["quantum_dm"].int(),
                    points(d["points"]),
                    d["markers"].list().map { m -> m.list().map { it.int() }.let { RouteMarker(it[0], it[1], it[2]) } },
                )
            }

            MsgType.TRAFFIC -> {
                Traffic(
                    d["route_id"].long(),
                    d["version"].int(),
                    d["entries"].list().map {
                        val e = it.list()
                        TrafficEntry(e[0].long(), e[1].long(), TrafficLevel.entries[e[2].int()])
                    },
                )
            }

            MsgType.CHUNK -> {
                Chunk(
                    ChunkKey(d["level"].int(), d["tile_x"].long(), d["tile_y"].long(), d["data_version"].long()),
                    Provenance.entries.single { it.code == d["provenance"].int() },
                    d["epoch"].int(),
                    d["origin_x_m"].int(),
                    d["origin_y_m"].int(),
                    d["quantum_dm"].int(),
                    d["extent"].int(),
                    shapes(d["lines"]),
                    shapes(d["polys"]),
                )
            }

            else -> {
                ChunkHave(d["epoch"].int(), d["entries"].list().map { key(it) })
            }
        }

    @Test
    fun validVectors() {
        val cases = vectors["valid"].list().map { it.obj() }
        assertTrue(cases.size >= 10)
        for (v in cases) {
            val name = v["name"].str()
            val bytes = v["hex"].str().hexBytes()
            val want = expected(v["type"].int(), v["decoded"].obj())
            assertArrayEquals(name, bytes, BinaryCodec.encode(want))
            assertEquals(name, want, BinaryCodec.decode(v["type"].int(), bytes))
        }
    }

    @Test
    fun invalidVectors() {
        val cases = vectors["invalid"].list().map { it.obj() }
        assertTrue(cases.size >= 10)
        for (v in cases) {
            val e =
                assertThrows(v["name"].str(), ProtocolException::class.java) {
                    BinaryCodec.decode(v["type"].int(), v["hex"].str().hexBytes())
                }
            assertEquals(v["name"].str(), v["error"].str(), e.reason)
        }
    }

    @Test
    fun varintEdges() {
        val head = byteArrayOf(1, 3, 0, 0, 0, 2, 0, 1)

        fun traffic(vararg tail: Int) = head + tail.map { it.toByte() }.toByteArray()
        val max = BinaryCodec.decode(MsgType.TRAFFIC, traffic(0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x01, 0x00)) as Traffic
        assertEquals(U32_MAX, max.entries.single().offsetM)
        for (bad in listOf(
            traffic(0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0x01, 0x00),
            traffic(0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x01, 0x00),
        )) {
            assertEquals(
                "invalid",
                assertThrows(ProtocolException::class.java) { BinaryCodec.decode(MsgType.TRAFFIC, bad) }.reason,
            )
        }
        assertEquals(
            "invalid",
            assertThrows(ProtocolException::class.java) {
                BinaryCodec.decode(MsgType.POSE, ByteArray(0))
            }.reason,
        )
    }

    @Test
    fun encodeChecksLimits() {
        assertThrows(IllegalArgumentException::class.java) { Pose(0, 0, 0, 0, 36_000, 0) }
        assertThrows(IllegalArgumentException::class.java) {
            Route(1, 0, Routing.GOOGLE, 7, 0, 10, listOf(Point(0, 0), Point(1 shl 30, 0)), emptyList())
        }
        assertThrows(IllegalArgumentException::class.java) {
            Traffic(1, 0, listOf(TrafficEntry(0, 10, TrafficLevel.FREE), TrafficEntry(5, 10, TrafficLevel.JAM)))
        }
    }
}
