package io.github.d4rthc0d3r.sarathi.core.links

/** Map short codes that decode offline, without asking the website. */
object ShortCodes {
    private const val OSM_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_~"
    private const val GE0_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
    private const val BITS_PER_CHAR = 3
    private const val GE0_COORD_BITS = 30
    private const val GE0_POINT_CHARS = 10

    /**
     * An osm.org/go/ code: 6 bits a character, alternating longitude and latitude bits (OSM's
     * ShortLink). Returns the south-west corner of the cell, within centimetres at full length.
     */
    fun osm(code: String): Coord? {
        var x = 0L
        var y = 0L
        var chars = 0
        for (ch in code.replace('@', '~')) {
            val digit = OSM_ALPHABET.indexOf(ch)
            if (digit < 0) break
            x = x shl BITS_PER_CHAR
            y = y shl BITS_PER_CHAR
            for (j in 2 downTo 0) {
                if (digit and (1 shl (j + j + 1)) != 0) x = x or (1L shl j)
                if (digit and (1 shl (j + j)) != 0) y = y or (1L shl j)
            }
            chars++
        }
        if (chars == 0) return null
        val scale = Math.pow(2.0, 2.0 - BITS_PER_CHAR * chars)
        @Suppress("MagicNumber") // OSM's encoding: 90° and 45° per unit at scale 1
        return Coord(lat = y * scale * 45 - 90, lng = x * scale * 90 - 180).takeIf { it.valid }
    }

    /**
     * An Organic Maps (ge0) code: a zoom character, then up to 9 characters of interleaved
     * latitude and longitude bits; the point is the middle of the encoded square.
     */
    @Suppress("MagicNumber") // which of a character's 6 bits belong to latitude and longitude
    fun ge0(code: String): Coord? {
        val point = code.drop(1)
        val digits = point.map { GE0_ALPHABET.indexOf(it) }
        if (point.isEmpty() || point.length >= GE0_POINT_CHARS || digits.any { it < 0 }) return null
        var lat = 0L
        var lng = 0L
        var shift = GE0_COORD_BITS - BITS_PER_CHAR
        for (a in digits) {
            lat = lat or (bits(a, 5, 3, 1) shl shift)
            lng = lng or (bits(a, 4, 2, 0) shl shift)
            shift -= BITS_PER_CHAR
        }
        val middle = 1L shl (BITS_PER_CHAR * (GE0_POINT_CHARS - point.length) - 1)
        val max = ((1L shl GE0_COORD_BITS) - 1).toDouble()
        @Suppress("MagicNumber") // degrees of latitude and longitude
        return Coord(lat = (lat + middle) / max * 180 - 90, lng = (lng + middle) / max * 360 - 180).takeIf { it.valid }
    }

    private fun bits(
        a: Int,
        high: Int,
        mid: Int,
        low: Int,
    ): Long = (((a shr high) and 1) shl 2 or (((a shr mid) and 1) shl 1) or ((a shr low) and 1)).toLong()
}
