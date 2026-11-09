package io.github.d4rthc0d3r.sarathi.core.links

/**
 * Google Maps' `data=` parameter: `!<field><type><value>` tokens, where type `m` holds the next
 * N tokens (all of them, nested ones included). Undocumented; read only as far as the recorded
 * links show (ADR-0008).
 */
internal object GoogleData {
    class Node(
        val field: Int,
        val type: Char,
        val value: String,
        val children: List<Node> = emptyList(),
    ) {
        fun all(): Sequence<Node> = sequenceOf(this) + children.asSequence().flatMap { it.all() }

        /** The first `!3d<lat>!4d<lng>` pair at or below this node. */
        fun coord(): Coord? {
            val nodes = all().toList()
            val lat = nodes.firstOrNull { it.field == LAT && it.type == 'd' }?.value?.toDoubleOrNull()
            val lng = nodes.firstOrNull { it.field == LNG && it.type == 'd' }?.value?.toDoubleOrNull()
            return if (lat != null && lng != null) Coord(lat, lng).takeIf { it.valid } else null
        }
    }

    private const val LAT = 3
    private const val LNG = 4
    private const val MODE = 3
    private val TOKEN = Regex("^(\\d+)([a-z])(.*)$")

    fun parse(data: String): List<Node> {
        val tokens = data.split('!').filter { it.isNotEmpty() }
        var i = 0

        fun node(): Node {
            val (field, type, value) = TOKEN.matchEntire(tokens[i++])?.destructured ?: return Node(0, '?', "")
            // An `m` token's number counts every token nested under it, not just its children.
            val children = mutableListOf<Node>()
            val end = i + if (type == "m") value.toIntOrNull() ?: 0 else 0
            while (i < minOf(end, tokens.size)) children += node()
            return Node(field.toInt(), type[0], value, children)
        }

        val nodes = mutableListOf<Node>()
        while (i < tokens.size) nodes += node()
        return nodes
    }

    /** A directions link's stops: `4m → 4m → 1m` blocks in order, each with its coordinates. */
    fun routeStops(nodes: List<Node>): List<Coord?> {
        val route =
            nodes
                .flatMap { it.all() }
                .firstOrNull { n -> n.type == 'm' && n.children.count { it.field == 1 && it.type == 'm' } >= 2 }
        return route
            ?.children
            ?.filter { it.field == 1 && it.type == 'm' }
            ?.map { it.coord() }
            .orEmpty()
    }

    /** `!3e<n>`: 0 car and 9 two-wheeler are recorded; others aren't (ADR-0008). */
    fun mode(nodes: List<Node>): SharedMode? =
        when (nodes.flatMap { it.all() }.lastOrNull { it.field == MODE && it.type == 'e' }?.value) {
            null -> null
            "0" -> SharedMode.DRIVING
            "9" -> SharedMode.TWO_WHEELER
            else -> SharedMode.UNKNOWN
        }
}
