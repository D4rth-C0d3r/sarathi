package io.github.d4rthc0d3r.sarathi.core.protocol

/**
 * JSON messages (docs/protocol/protocol.md section 3): decoding with the schemas' checks, and
 * the canonical encoding, in both directions.
 */
object JsonCodec {
    const val MAX_JSON = 2048

    private val phoneReaders: Map<Int, (Fields) -> JsonMessage> =
        mapOf(
            MsgType.HELLO to HelloPhone::read,
            MsgType.TIME to TimeSync::read,
            MsgType.PHONE_BATT to PhoneBattery::read,
            MsgType.FRAME to FrameEpoch::read,
            MsgType.BACKEND to BackendInfo::read,
            MsgType.EVENT to Event::read,
            MsgType.NAV to Nav::read,
            MsgType.NAV_END to NavEnd::read,
            MsgType.CALL to Call::read,
            MsgType.MEDIA to Media::read,
        )

    private val deviceReaders: Map<Int, (Fields) -> JsonMessage> =
        mapOf(
            MsgType.HELLO to HelloDevice::read,
            MsgType.CHUNK_ACK to ChunkAck::read,
            MsgType.FLOW to Flow::read,
            MsgType.STYLE to Style::read,
            MsgType.CALL_ACTION to CallAction::read,
            MsgType.MEDIA_CMD to MediaCmd::read,
        )

    /** Decodes a message sent by [sender]; throws [ProtocolException] with the drop reason. */
    fun decode(
        type: Int,
        payload: ByteArray,
        sender: Sender,
    ): JsonMessage {
        val reader =
            (if (sender == Sender.PHONE) phoneReaders else deviceReaders)[type]
                ?: drop("unknown_type")
        if (payload.size > MAX_JSON) drop("too_long")
        val value =
            try {
                StrictJsonParser.parse(payload)
            } catch (e: JsonSyntaxException) {
                drop("not_json", e.message ?: "", e)
            } catch (e: JsonDuplicateNameException) {
                drop("invalid", e.message ?: "", e)
            }
        if (value !is JsonObject || (value["t"] as? JsonString)?.value != MsgType.name(type)) {
            drop("type_mismatch")
        }
        return try {
            reader(Fields(value))
        } catch (e: IllegalArgumentException) {
            drop("invalid", e.message ?: "", e)
        }
    }

    /** The canonical encoding. Throws if it would exceed [MAX_JSON] bytes. */
    fun encode(message: JsonMessage): ByteArray {
        val w = CanonicalJsonWriter()
        w.beginObject()
        w.field("t", checkNotNull(MsgType.name(message.type)))
        message.write(w)
        w.endObject()
        val bytes = w.toBytes()
        require(bytes.size <= MAX_JSON) { "${bytes.size} bytes, over $MAX_JSON" }
        return bytes
    }
}

/** Typed access to an object's members; every getter throws IllegalArgumentException. */
class Fields(
    val obj: JsonObject,
) {
    fun has(name: String): Boolean = obj.members.containsKey(name)

    fun long(
        name: String,
        range: LongRange,
    ): Long = longOf(obj[name], range, name)

    fun int(
        name: String,
        range: IntRange,
    ): Int = long(name, range.first.toLong()..range.last.toLong()).toInt()

    fun bool(name: String): Boolean = requireNotNull(obj[name] as? JsonBool) { "$name: not a boolean" }.value

    fun text(name: String): String = textOf(obj[name], name)

    fun array(name: String): List<JsonValue> = requireNotNull(obj[name] as? JsonArray) { "$name: not an array" }.items

    fun child(name: String): Fields = Fields(requireNotNull(obj[name] as? JsonObject) { "$name: not an object" })

    inline fun <reified E> enum(name: String): E where E : Enum<E>, E : Wire = wireValue(text(name))

    inline fun <reified E> set(
        name: String,
        maxItems: Int,
    ): Set<E> where E : Enum<E>, E : Wire {
        val items = array(name)
        require(items.size <= maxItems) { "$name: over $maxItems items" }
        return items.map { wireValue<E>(textOf(it, name)) }.toSet()
    }

    companion object {
        fun longOf(
            value: JsonValue?,
            range: LongRange,
            name: String,
        ): Long {
            val v = (value as? JsonNumber)?.toLongOrNull()
            require(v != null && v in range) { "$name: not an integer in $range" }
            return v
        }

        fun textOf(
            value: JsonValue?,
            name: String,
        ): String = requireNotNull(value as? JsonString) { "$name: not a string" }.value
    }
}

fun CanonicalJsonWriter.field(
    name: String,
    value: String,
) {
    key(name)
    string(value)
}

fun CanonicalJsonWriter.field(
    name: String,
    value: Long,
) {
    key(name)
    number(value)
}

fun CanonicalJsonWriter.field(
    name: String,
    value: Boolean,
) {
    key(name)
    bool(value)
}

/** A set, in the enum's (the schema's) order. */
fun <E> CanonicalJsonWriter.field(
    name: String,
    values: Set<E>,
) where E : Enum<E>, E : Wire {
    key(name)
    beginArray()
    values.sortedBy { it.ordinal }.forEach { string(it.wire) }
    endArray()
}
