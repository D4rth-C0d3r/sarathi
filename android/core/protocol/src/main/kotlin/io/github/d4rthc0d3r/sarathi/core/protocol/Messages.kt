package io.github.d4rthc0d3r.sarathi.core.protocol

/** Message type codes (docs/protocol/protocol.md section 2). */
object MsgType {
    const val HELLO = 0x01
    const val TIME = 0x02
    const val PHONE_BATT = 0x03
    const val FRAME = 0x04
    const val BACKEND = 0x05
    const val EVENT = 0x06
    const val NAV = 0x07
    const val NAV_END = 0x08
    const val CALL = 0x09
    const val MEDIA = 0x0A
    const val LOCK = 0x0B // since 1.1
    const val CHUNK_ACK = 0x10
    const val FLOW = 0x11
    const val STYLE = 0x12
    const val CALL_ACTION = 0x13
    const val MEDIA_CMD = 0x14
    const val POSE = 0x40
    const val ROUTE = 0x41
    const val TRAFFIC = 0x42
    const val CHUNK = 0x43
    const val CHUNK_HAVE = 0x44

    private val names =
        mapOf(
            HELLO to "hello",
            TIME to "time",
            PHONE_BATT to "phone_batt",
            FRAME to "frame",
            BACKEND to "backend",
            EVENT to "event",
            NAV to "nav",
            NAV_END to "nav_end",
            CALL to "call",
            MEDIA to "media",
            LOCK to "lock",
            CHUNK_ACK to "chunk_ack",
            FLOW to "flow",
            STYLE to "style",
            CALL_ACTION to "call_action",
            MEDIA_CMD to "media_cmd",
            POSE to "pose",
            ROUTE to "route",
            TRAFFIC to "traffic",
            CHUNK to "chunk",
            CHUNK_HAVE to "chunk_have",
        )

    /** The message name ("hello", "nav", ...), or null for an unknown type. */
    fun name(type: Int): String? = names[type]
}

object ProtocolVersion {
    const val MAJOR = 1
    const val MINOR = 1
}

/** Which side sends a message; the vectors' `direction`. */
enum class Sender(
    val wire: String,
) {
    PHONE("phone"),
    DEVICE("device"),
}

/** Why a received message was dropped; `reason` is the vectors' `error` name. */
class ProtocolException(
    val reason: String,
    detail: String = reason,
    cause: Throwable? = null,
) : Exception(detail, cause)

/** Throws a [ProtocolException]: the message is dropped for [reason]. */
fun drop(
    reason: String,
    detail: String = reason,
    cause: Throwable? = null,
): Nothing = throw ProtocolException(reason, detail, cause)

/** An enumeration value with its name on the wire. Declaration order is the schema's order. */
interface Wire {
    val wire: String
}

inline fun <reified E> wireValue(name: String): E where E : Enum<E>, E : Wire =
    requireNotNull(enumValues<E>().firstOrNull { it.wire == name }) { "unknown value '$name'" }

enum class Routing(
    override val wire: String,
) : Wire {
    GOOGLE("google"),
    MAPPLS("mappls"),
    OSM_ONLINE("osm_online"),
    OSM_OFFLINE("osm_offline"),
}

/** Search backends; also `event.backend`. */
enum class Search(
    override val wire: String,
) : Wire {
    GOOGLE("google"),
    MAPPLS("mappls"),
    OSM("osm"),
}

enum class EventKind(
    override val wire: String,
) : Wire {
    FALLBACK("fallback"),
    REROUTE("reroute"),
    COST_WARNING("cost_warning"),
}

enum class FallbackReason(
    override val wire: String,
) : Wire {
    QUOTA("quota"),
    OFFLINE("offline"),
    ERROR("error"),
    NO_ROUTE("no_route"),
    ACCOUNT("account"),
}

enum class NavEndReason(
    override val wire: String,
) : Wire {
    ARRIVED("arrived"),
    CANCELLED("cancelled"),
    ERROR("error"),
}

enum class CallPhase(
    override val wire: String,
) : Wire {
    RINGING("ringing"),
    ACTIVE("active"),
    IDLE("idle"),
}

enum class MapStyle(
    override val wire: String,
) : Wire {
    CLASSIC("classic"),
    HUD("hud"),
    RACING("racing"),
    MINIMAL("minimal"),
}

enum class RefuseReason(
    override val wire: String,
) : Wire {
    FULL("full"),
    INVALID("invalid"),
    EPOCH("epoch"),
}

enum class CallChoice(
    override val wire: String,
) : Wire {
    ACCEPT("accept"),
    REJECT("reject"),
}

enum class MediaKey(
    override val wire: String,
) : Wire {
    PLAY_PAUSE("play_pause"),
    NEXT("next"),
    PREV("prev"),
}

enum class PhoneCap(
    override val wire: String,
) : Wire {
    GOOGLE("google"),
    MAPPLS("mappls"),
    OSM("osm"),
    OFFLINE("offline"),
    TRAFFIC("traffic"),
    CALLS("calls"),
    MEDIA("media"),
}

enum class NavFlag(
    override val wire: String,
) : Wire {
    OFF_ROUTE("off_route"),
    REROUTING("rerouting"),
    ARRIVED("arrived"),
}

enum class MapLayer(
    override val wire: String,
) : Wire {
    WATER("water"),
    PARKS("parks"),
}

/** Maneuvers; the ordinal is the code in the binary `route` message. */
enum class Maneuver(
    override val wire: String,
) : Wire {
    NONE("none"),
    DEPART("depart"),
    STRAIGHT("straight"),
    SLIGHT_LEFT("slight_left"),
    LEFT("left"),
    SHARP_LEFT("sharp_left"),
    SLIGHT_RIGHT("slight_right"),
    RIGHT("right"),
    SHARP_RIGHT("sharp_right"),
    UTURN("uturn"),
    ROUNDABOUT("roundabout"),
    ARRIVE("arrive"),
    KEEP_LEFT("keep_left"),
    KEEP_RIGHT("keep_right"),
    MERGE("merge"),
    FORK_LEFT("fork_left"),
    FORK_RIGHT("fork_right"),
    RAMP_LEFT("ramp_left"),
    RAMP_RIGHT("ramp_right"),
    FERRY("ferry"),
    ;

    val code: Int get() = ordinal

    companion object {
        /** The maneuver an `m` name stands for; an unknown one (a newer minor version) is STRAIGHT. */
        fun fromName(name: String): Maneuver = entries.firstOrNull { it.wire == name } ?: STRAIGHT
    }
}

/** A map chunk's identity, in `chunk_ack`, `chunk` and `chunk_have`. */
data class ChunkKey(
    val level: Int,
    val tileX: Long,
    val tileY: Long,
    val dataVersion: Long,
) {
    init {
        require(level in 0..1) { "level" }
        requireU32(tileX, "tile_x")
        requireU32(tileY, "tile_y")
        requireU32(dataVersion, "data_version")
    }
}

const val U8_MAX = 0xFF
const val U16_MAX = 0xFFFF
const val U32_MAX = 0xFFFF_FFFFL

fun requireU32(
    value: Long,
    name: String,
) = require(value in 0..U32_MAX) { name }

fun requireU16(
    value: Int,
    name: String,
) = require(value in 0..U16_MAX) { name }

/**
 * Text the protocol carries: at most [maxBytes] bytes of UTF-8, without U+0000 or an
 * unpaired surrogate (protocol.md section 3).
 */
fun requireText(
    value: String,
    maxBytes: Int,
    name: String,
) {
    require(value.none { it == '\u0000' } && wellFormed(value)) { "$name: bad character" }
    require(value.toByteArray(Charsets.UTF_8).size <= maxBytes) { "$name: over $maxBytes bytes" }
}

private fun wellFormed(s: String): Boolean {
    var i = 0
    while (i < s.length) {
        val c = s[i]
        val pairOk = !c.isSurrogate() || (c.isHighSurrogate() && i + 1 < s.length && s[i + 1].isLowSurrogate())
        if (!pairOk) return false
        i += if (c.isHighSurrogate()) 2 else 1
    }
    return true
}
