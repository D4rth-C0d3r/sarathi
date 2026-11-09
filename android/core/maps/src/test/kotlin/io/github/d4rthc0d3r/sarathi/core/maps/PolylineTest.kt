package io.github.d4rthc0d3r.sarathi.core.maps

import org.junit.Assert.assertEquals
import org.junit.Test

class PolylineTest {
    @Test
    fun decodesGooglesExample() {
        // From Google's "Encoded Polyline Algorithm Format" page.
        val points = Polyline.decode("_p~iF~ps|U_ulLnnqC_mqNvxq`@", 5)
        assertEquals(listOf(LatLng(38.5, -120.2), LatLng(40.7, -120.95), LatLng(43.252, -126.453)), points)
    }
}
