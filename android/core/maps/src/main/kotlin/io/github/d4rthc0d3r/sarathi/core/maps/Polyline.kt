package io.github.d4rthc0d3r.sarathi.core.maps

/** Google's encoded polyline format, used by Valhalla (precision 6) and GraphHopper (5). */
object Polyline {
    private const val CHUNK = 0x1f
    private const val MORE = 0x20
    private const val BITS = 5
    private const val OFFSET = 63
    private const val DECIMAL = 10.0

    fun decode(
        encoded: String,
        precision: Int,
    ): List<LatLng> {
        val factor = Math.pow(DECIMAL, precision.toDouble())
        val points = mutableListOf<LatLng>()
        var i = 0
        var lat = 0L
        var lng = 0L

        fun next(): Long {
            var result = 0L
            var shift = 0
            var b: Int
            do {
                b = encoded[i++].code - OFFSET
                result = result or ((b and CHUNK).toLong() shl shift)
                shift += BITS
            } while (b >= MORE && i < encoded.length)
            return if (result and 1L != 0L) (result shr 1).inv() else result shr 1
        }
        while (i < encoded.length) {
            lat += next()
            lng += next()
            points += LatLng(lat / factor, lng / factor)
        }
        return points
    }
}
