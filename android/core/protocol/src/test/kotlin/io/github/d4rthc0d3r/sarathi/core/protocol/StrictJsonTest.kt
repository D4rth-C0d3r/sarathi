package io.github.d4rthc0d3r.sarathi.core.protocol

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Test

class StrictJsonTest {
    @Test
    fun parsesNestedValuesInOrder() {
        val v = StrictJsonParser.parse("""{"b":[1,-2,{"c":true}],"a":"\u0915\"","n":null}""").obj()
        assertEquals(listOf("b", "a", "n"), v.members.keys.toList())
        assertEquals(-2L, v["b"].list()[1].long())
        assertEquals(JsonBool(true), v["b"].list()[2].obj()["c"])
        assertEquals("क\"", v["a"].str())
        assertEquals(JsonNull, v["n"])
    }

    @Test
    fun rejectsWhatTheFirmwareRejects() {
        for (bad in listOf(
            "[1 2]",
            "{\"a\":1,}",
            "01",
            "{\"a\":01}",
            "\"a\u0001\"",
            "[1]x",
            "{\"a\" 1}",
            "nul",
            "-",
            "1.",
            "1e",
        )) {
            assertThrows(bad, JsonSyntaxException::class.java) { StrictJsonParser.parse(bad) }
        }
        assertThrows(JsonSyntaxException::class.java) { StrictJsonParser.parse(byteArrayOf(0x22, 0xC3.toByte(), 0x22)) }
        assertThrows(JsonDuplicateNameException::class.java) { StrictJsonParser.parse("""{"a":1,"\u0061":2}""") }
    }

    @Test
    fun integersOnlyWhenWhole() {
        assertEquals(9007199254740992L, JsonNumber("9007199254740992").toLongOrNull())
        assertNull(JsonNumber("9007199254740993").toLongOrNull())
        assertNull(JsonNumber("7.0").toLongOrNull())
        assertNull(JsonNumber("1e3").toLongOrNull())
        assertEquals(0L, JsonNumber("-0").toLongOrNull())
    }

    @Test
    fun writerIsCanonical() {
        val w = CanonicalJsonWriter()
        w.beginArray()
        w.number(Long.MIN_VALUE)
        w.string("\u001f\u007f/\r\"\\é")
        w.beginObject()
        w.key("k")
        w.nul()
        w.endObject()
        w.bool(false)
        w.endArray()
        assertEquals("[-9223372036854775808,\"\\u001f\u007f/\\r\\\"\\\\é\",{\"k\":null},false]", String(w.toBytes()))
    }
}
