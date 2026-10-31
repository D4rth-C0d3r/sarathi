package io.github.d4rthc0d3r.sarathi.core.protocol

// ---- Device → phone JSON messages (protocol.md section 3).

/** The route the device holds, in `hello`. */
data class RouteRef(
    val id: Long,
    val version: Int,
) {
    init {
        requireU32(id, "route.id")
        requireU16(version, "route.v")
    }
}

data class HelloDevice(
    val fw: String,
    val seq: Long,
    val styles: List<String>,
    val chunkKb: Int,
    val maxMsg: Int,
    val route: RouteRef?,
    val epoch: Int?,
    val protoMajor: Int = ProtocolVersion.MAJOR,
    val protoMinor: Int = ProtocolVersion.MINOR,
) : JsonMessage {
    override val type get() = MsgType.HELLO

    init {
        require(protoMajor in 0..U8_MAX && protoMinor in 0..U8_MAX) { "proto" }
        requireText(fw, FW_BYTES, "fw")
        requireU32(seq, "seq")
        require(styles.size <= MAX_STYLES) { "styles" }
        styles.forEach { requireText(it, STYLE_BYTES, "styles") }
        requireU16(chunkKb, "chunk_kb")
        require(maxMsg in 1..Framing.MAX_MESSAGE) { "max_msg" }
        epoch?.let { requireU16(it, "epoch") }
    }

    override fun write(w: CanonicalJsonWriter) {
        w.key("proto")
        w.beginArray()
        w.number(protoMajor.toLong())
        w.number(protoMinor.toLong())
        w.endArray()
        w.field("fw", fw)
        w.field("seq", seq)
        w.key("caps")
        w.beginObject()
        w.key("styles")
        w.beginArray()
        styles.forEach { w.string(it) }
        w.endArray()
        w.field("chunk_kb", chunkKb.toLong())
        w.field("max_msg", maxMsg.toLong())
        w.endObject()
        w.key("route")
        if (route == null) {
            w.nul()
        } else {
            w.beginObject()
            w.field("id", route.id)
            w.field("v", route.version.toLong())
            w.endObject()
        }
        w.key("epoch")
        if (epoch == null) w.nul() else w.number(epoch.toLong())
    }

    companion object {
        private const val FW_BYTES = 16
        private const val STYLE_BYTES = 16
        private const val MAX_STYLES = 8

        fun read(f: Fields): HelloDevice {
            val proto = f.array("proto")
            require(proto.size == 2) { "proto: two numbers" }
            val caps = f.child("caps")
            return HelloDevice(
                fw = f.text("fw"),
                seq = f.long("seq", 0..U32_MAX),
                styles = caps.array("styles").map { Fields.textOf(it, "styles") },
                chunkKb = caps.int("chunk_kb", 0..U16_MAX),
                maxMsg = caps.int("max_msg", 1..Framing.MAX_MESSAGE),
                route = if (f.obj["route"] == JsonNull) null else readRoute(f.child("route")),
                epoch = if (f.obj["epoch"] == JsonNull) null else f.int("epoch", 0..U16_MAX),
                protoMajor = Fields.longOf(proto[0], 0L..U8_MAX, "proto").toInt(),
                protoMinor = Fields.longOf(proto[1], 0L..U8_MAX, "proto").toInt(),
            )
        }

        private fun readRoute(f: Fields) = RouteRef(f.long("id", 0..U32_MAX), f.int("v", 0..U16_MAX))
    }
}

data class RefusedChunk(
    val key: ChunkKey,
    val reason: RefuseReason,
)

data class ChunkAck(
    val stored: List<ChunkKey>,
    val refused: List<RefusedChunk>,
) : JsonMessage {
    override val type get() = MsgType.CHUNK_ACK

    init {
        require(stored.size <= MAX_ENTRIES && refused.size <= MAX_ENTRIES) { "over $MAX_ENTRIES entries" }
    }

    override fun write(w: CanonicalJsonWriter) {
        w.key("stored")
        w.beginArray()
        stored.forEach {
            w.beginArray()
            w.chunkKey(it)
            w.endArray()
        }
        w.endArray()
        w.key("refused")
        w.beginArray()
        refused.forEach {
            w.beginArray()
            w.chunkKey(it.key)
            w.string(it.reason.wire)
            w.endArray()
        }
        w.endArray()
    }

    companion object {
        private const val MAX_ENTRIES = 64
        private const val KEY_ITEMS = 4

        fun read(f: Fields) =
            ChunkAck(
                f.array("stored").map { readKey(it, KEY_ITEMS) },
                f.array("refused").map {
                    RefusedChunk(
                        readKey(it, KEY_ITEMS + 1),
                        wireValue(Fields.textOf((it as JsonArray).items[KEY_ITEMS], "reason")),
                    )
                },
            )

        private fun readKey(
            value: JsonValue,
            size: Int,
        ): ChunkKey {
            val items = requireNotNull(value as? JsonArray) { "chunk key: not an array" }.items
            require(items.size == size) { "chunk key: $size items" }
            val n = items.map { (it as? JsonNumber)?.toLongOrNull() }
            return ChunkKey(
                Fields.longOf(items[0], 0L..1L, "level").toInt(),
                requireNotNull(n[1]) { "tile_x" },
                requireNotNull(n[2]) { "tile_y" },
                requireNotNull(n[KEY_ITEMS - 1]) { "data_version" },
            )
        }
    }
}

private fun CanonicalJsonWriter.chunkKey(k: ChunkKey) {
    number(k.level.toLong())
    number(k.tileX)
    number(k.tileY)
    number(k.dataVersion)
}

data class Flow(
    val window: Int,
    val consumed: Long,
) : JsonMessage {
    override val type get() = MsgType.FLOW

    init {
        require(window in 1..Framing.MAX_FRAGMENTS) { "window" }
        requireU32(consumed, "consumed")
    }

    override fun write(w: CanonicalJsonWriter) {
        w.field("window", window.toLong())
        w.field("consumed", consumed)
    }

    companion object {
        fun read(f: Fields) = Flow(f.int("window", 1..Framing.MAX_FRAGMENTS), f.long("consumed", 0..U32_MAX))
    }
}

data class Style(
    val map: MapStyle,
    val layers: Set<MapLayer>,
) : JsonMessage {
    override val type get() = MsgType.STYLE

    override fun write(w: CanonicalJsonWriter) {
        w.field("map", map.wire)
        w.field("layers", layers)
    }

    companion object {
        fun read(f: Fields) = Style(f.enum("map"), f.set("layers", MapLayer.entries.size))
    }
}

data class CallAction(
    val action: CallChoice,
) : JsonMessage {
    override val type get() = MsgType.CALL_ACTION

    override fun write(w: CanonicalJsonWriter) = w.field("action", action.wire)

    companion object {
        fun read(f: Fields) = CallAction(f.enum("action"))
    }
}

data class MediaCmd(
    val cmd: MediaKey,
) : JsonMessage {
    override val type get() = MsgType.MEDIA_CMD

    override fun write(w: CanonicalJsonWriter) = w.field("cmd", cmd.wire)

    companion object {
        fun read(f: Fields) = MediaCmd(f.enum("cmd"))
    }
}
