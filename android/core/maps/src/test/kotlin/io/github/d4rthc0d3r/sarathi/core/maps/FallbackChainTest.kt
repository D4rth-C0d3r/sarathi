package io.github.d4rthc0d3r.sarathi.core.maps

import kotlinx.coroutines.delay
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class FallbackChainTest {
    private class Fake(
        val name: String,
        val blocked: Unavailable? = null,
        val fail: Unavailable? = null,
        val slowMs: Long = 0,
    ) {
        var calls = 0
    }

    private suspend fun run(vararg fakes: Fake?): ChainResult<String, String> {
        val byName = fakes.filterNotNull().associateBy { it.name }
        return FallbackChain.run(
            listOf("google", "mappls", "valhalla", "offline"),
            { byName[it] },
            { it.blocked },
            1_000,
        ) {
            it.calls++
            delay(it.slowMs)
            it.fail?.let { reason -> throw ProviderException(reason) }
            it.name
        }
    }

    @Test
    fun theADRsForcedFailuresWalkTheChain() =
        runTest {
            val google = Fake("google", blocked = Unavailable.OVER_QUOTA)
            val mappls = Fake("mappls", fail = Unavailable.OVER_QUOTA)
            val valhalla = Fake("valhalla", slowMs = 5_000)
            val offline = Fake("offline")
            val result = run(google, mappls, valhalla, offline)
            assertEquals("offline", result.value)
            assertEquals(
                listOf(Unavailable.OVER_QUOTA, Unavailable.OVER_QUOTA, Unavailable.TIMEOUT),
                result.skipped.map { it.reason },
            )
            assertEquals(0, google.calls) // blocked locally: never called
        }

    @Test
    fun aMissingProviderIsSkippedAsNoKey() =
        runTest {
            val result = run(null, Fake("mappls"))
            assertEquals("mappls", result.backend)
            assertEquals(listOf(Unavailable.NO_KEY), result.skipped.map { it.reason })
        }

    @Test
    fun nothingLeft() =
        runTest {
            val result = run(Fake("google", fail = Unavailable.NO_ROUTE))
            assertNull(result.value)
            assertEquals(4, result.skipped.size)
        }
}
