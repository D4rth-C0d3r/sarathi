package io.github.d4rthc0d3r.sarathi.core.protocol

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/** Framing against docs/protocol/vectors/crc16.json and framing.json. */
class FrameTest {
    @Test
    fun crcVectors() {
        val cases = Vectors.load("crc16.json")["cases"].list()
        assertTrue(cases.isNotEmpty())
        for (c in cases) {
            val want =
                c
                    .obj()["crc"]
                    .str()
                    .removePrefix("0x")
                    .toInt(16)
            assertEquals(want, Framing.crc16(c.obj()["input"].str().hexBytes()))
        }
    }

    @Test
    fun sendVectors() {
        val cases = Vectors.load("framing.json")["send"].list()
        assertTrue(cases.isNotEmpty())
        for (c in cases.map { it.obj() }) {
            val name = c["name"].str()
            val frames =
                Framing.fragment(
                    c["type"].int(),
                    c["msg_id"].int(),
                    c["payload"].str().hexBytes(),
                    c["mtu"].int(),
                )
            assertEquals(name, c["frames"].list().map { it.str() }, frames.map { it.hex() })
        }
    }

    @Test
    fun receiveVectors() {
        val cases = Vectors.load("framing.json")["receive"].list()
        assertTrue(cases.isNotEmpty())
        for (c in cases.map { it.obj() }) {
            val want =
                c["events"].list().map { e ->
                    val drop = e.obj()["drop"]
                    if (drop != null) {
                        "drop:${drop.str()}"
                    } else {
                        val m = e.obj()["message"].obj()
                        "message:${m["type"].int()}:${m["msg_id"].int()}:${m["payload"].str()}"
                    }
                }
            val r = Reassembler()
            val got =
                c["frames"].list().flatMap { f ->
                    r.feed(f.str().hexBytes()).map { e ->
                        when (e) {
                            is FrameEvent.Dropped -> "drop:${e.reason.wire}"
                            is FrameEvent.Message -> "message:${e.type}:${e.msgId}:${e.payload.hex()}"
                        }
                    }
                }
            assertEquals(c["name"].str(), want, got)
        }
    }

    @Test
    fun roundTrip() {
        val payload = ByteArray(Framing.MAX_MESSAGE) { (it * 7 + 3).toByte() }
        for (mtu in listOf(247, 517)) {
            for (size in listOf(0, 1, 237, 238, 5000, Framing.MAX_MESSAGE)) {
                val r = Reassembler()
                val events = Framing.fragment(0x43, 9, payload.copyOf(size), mtu).flatMap { r.feed(it) }
                assertEquals(1, events.size)
                assertArrayEquals(payload.copyOf(size), (events.single() as FrameEvent.Message).payload)
            }
        }
    }

    @Test
    fun fragmentLimits() {
        assertEquals(64, Framing.fragment(1, 1, ByteArray(Framing.MAX_MESSAGE), 247).size)
        assertThrows(IllegalArgumentException::class.java) {
            Framing.fragment(1, 1, ByteArray(Framing.MAX_MESSAGE + 1), 517)
        }
        assertThrows(IllegalArgumentException::class.java) {
            Framing.fragment(1, 1, ByteArray(Framing.MAX_MESSAGE), 200)
        }
        assertThrows(IllegalArgumentException::class.java) { Framing.fragment(1, 1, ByteArray(1), 10) }
    }
}
