package io.github.d4rthc0d3r.sarathi.core.osmcontrib

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class RawGpsFixTest {
    private fun from(
        provider: String?,
        accuracy: Float? = 5f,
        lat: Double = 15.5,
        lon: Double = 73.8,
        time: Long = 1_700_000_000_000,
    ) = RawGpsFix.fromProvider(provider, lat, lon, time, accuracy, 10f)

    @Test
    fun `only satellite fixes make a track fix`() {
        assertNotNull(from("gps"))
        assertNull(from("fused")) // mixes in Google's Wi-Fi and cell positions
        assertNull(from("network"))
        assertNull(from("passive"))
        assertNull(from(null))
        assertNull(from("GPS")) // Android's name is lower case; nothing else passes
    }

    @Test
    fun `a fix needs an accuracy, a position and a time`() {
        assertNull(from("gps", accuracy = null))
        assertNull(from("gps", lat = 91.0))
        assertNull(from("gps", lon = -181.0))
        assertNull(from("gps", time = 0))
    }

    @Test
    fun `the gps provider name matches Android's`() {
        // android.location.LocationManager.GPS_PROVIDER, which this pure module can't import.
        assertEquals("gps", RawGpsFix.GNSS_PROVIDER)
    }
}
