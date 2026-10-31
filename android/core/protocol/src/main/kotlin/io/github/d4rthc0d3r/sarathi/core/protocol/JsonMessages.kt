package io.github.d4rthc0d3r.sarathi.core.protocol

/**
 * The JSON messages (protocol.md section 3). Each checks the schema's limits when built, so
 * a message that encodes is valid, and decoding is building from the received fields.
 */
sealed interface JsonMessage {
    val type: Int

    /** Writes the fields after "t", in canonical order. */
    fun write(w: CanonicalJsonWriter)
}

private const val PROTO_ITEMS = 2

private fun readProto(f: Fields): Pair<Int, Int> {
    val items = f.array("proto")
    require(items.size == PROTO_ITEMS) { "proto: two numbers" }
    return Fields.longOf(items[0], 0L..U8_MAX, "proto").toInt() to Fields.longOf(items[1], 0L..U8_MAX, "proto").toInt()
}

private fun CanonicalJsonWriter.proto(
    major: Int,
    minor: Int,
) {
    key("proto")
    beginArray()
    number(major.toLong())
    number(minor.toLong())
    endArray()
}

// ---- Phone → device.

data class HelloPhone(
    val app: String,
    val caps: Set<PhoneCap>,
    val protoMajor: Int = ProtocolVersion.MAJOR,
    val protoMinor: Int = ProtocolVersion.MINOR,
) : JsonMessage {
    override val type get() = MsgType.HELLO

    init {
        require(protoMajor in 0..U8_MAX && protoMinor in 0..U8_MAX) { "proto" }
        requireText(app, APP_BYTES, "app")
    }

    override fun write(w: CanonicalJsonWriter) {
        w.proto(protoMajor, protoMinor)
        w.field("app", app)
        w.field("caps", caps)
    }

    companion object {
        const val APP_BYTES = 32
        private const val MAX_CAPS = 16

        fun read(f: Fields): HelloPhone {
            val (major, minor) = readProto(f)
            return HelloPhone(f.text("app"), f.set("caps", MAX_CAPS), major, minor)
        }
    }
}

data class TimeSync(
    val local: String,
    val utcOffsetMin: Int,
) : JsonMessage {
    override val type get() = MsgType.TIME

    init {
        require(LOCAL.matches(local)) { "local: not YYYY-MM-DDTHH:MM:SS" }
        require(utcOffsetMin in OFFSET_RANGE) { "utc_offset_min" }
    }

    override fun write(w: CanonicalJsonWriter) {
        w.field("local", local)
        w.field("utc_offset_min", utcOffsetMin.toLong())
    }

    companion object {
        private val LOCAL = Regex("[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}")
        private val OFFSET_RANGE = -720..840

        fun read(f: Fields) = TimeSync(f.text("local"), f.int("utc_offset_min", Int.MIN_VALUE..Int.MAX_VALUE))
    }
}

data class PhoneBattery(
    val level: Int,
    val charging: Boolean,
) : JsonMessage {
    override val type get() = MsgType.PHONE_BATT

    init {
        require(level in 0..PERCENT) { "level" }
    }

    override fun write(w: CanonicalJsonWriter) {
        w.field("level", level.toLong())
        w.field("charging", charging)
    }

    companion object {
        fun read(f: Fields) = PhoneBattery(f.int("level", 0..PERCENT), f.bool("charging"))
    }
}

private const val PERCENT = 100

data class FrameEpoch(
    val epoch: Int,
) : JsonMessage {
    override val type get() = MsgType.FRAME

    init {
        requireU16(epoch, "epoch")
    }

    override fun write(w: CanonicalJsonWriter) = w.field("epoch", epoch.toLong())

    companion object {
        fun read(f: Fields) = FrameEpoch(f.int("epoch", 0..U16_MAX))
    }
}

data class BackendInfo(
    val routing: Routing,
    val search: Search,
    val name: String,
    val attribution: String,
    val traffic: Boolean,
    val twoWheeler: Boolean,
) : JsonMessage {
    override val type get() = MsgType.BACKEND

    init {
        requireText(name, NAME_BYTES, "name")
        requireText(attribution, ATTRIBUTION_BYTES, "attribution")
    }

    override fun write(w: CanonicalJsonWriter) {
        w.field("routing", routing.wire)
        w.field("search", search.wire)
        w.field("name", name)
        w.field("attribution", attribution)
        w.field("traffic", traffic)
        w.field("two_wheeler", twoWheeler)
    }

    companion object {
        const val NAME_BYTES = 24
        const val ATTRIBUTION_BYTES = 64

        fun read(f: Fields) =
            BackendInfo(
                f.enum("routing"),
                f.enum("search"),
                f.text("name"),
                f.text("attribution"),
                f.bool("traffic"),
                f.bool("two_wheeler"),
            )
    }
}

/** `from`, `to` and `reason` are required for FALLBACK; `backend` and `percent` for COST_WARNING. */
data class Event(
    val kind: EventKind,
    val from: Routing? = null,
    val to: Routing? = null,
    val reason: FallbackReason? = null,
    val backend: Search? = null,
    val percent: Int? = null,
) : JsonMessage {
    override val type get() = MsgType.EVENT

    init {
        if (kind == EventKind.FALLBACK) require(from != null && to != null && reason != null) { "fallback fields" }
        if (kind == EventKind.COST_WARNING) require(backend != null && percent != null) { "cost_warning fields" }
        require(percent == null || percent in 0..PERCENT) { "percent" }
    }

    override fun write(w: CanonicalJsonWriter) {
        w.field("kind", kind.wire)
        from?.let { w.field("from", it.wire) }
        to?.let { w.field("to", it.wire) }
        reason?.let { w.field("reason", it.wire) }
        backend?.let { w.field("backend", it.wire) }
        percent?.let { w.field("percent", it.toLong()) }
    }

    companion object {
        fun read(f: Fields) =
            Event(
                f.enum("kind"),
                if (f.has("from")) f.enum<Routing>("from") else null,
                if (f.has("to")) f.enum<Routing>("to") else null,
                if (f.has("reason")) f.enum<FallbackReason>("reason") else null,
                if (f.has("backend")) f.enum<Search>("backend") else null,
                if (f.has("percent")) f.int("percent", 0..PERCENT) else null,
            )
    }
}

/** [m] is kept as sent so an unknown maneuver survives; [maneuver] is what to draw. */
data class Nav(
    val m: String,
    val exit: Int,
    val distM: Long,
    val street: String,
    val remS: Long,
    val remM: Long,
    val flags: Set<NavFlag>,
) : JsonMessage {
    override val type get() = MsgType.NAV

    val maneuver: Maneuver get() = Maneuver.fromName(m)

    init {
        requireText(m, M_BYTES, "m")
        require(exit in 0..MAX_EXIT) { "exit" }
        require(distM in 0..MAX_DIST_M) { "dist_m" }
        requireText(street, STREET_BYTES, "street")
        require(remS in 0..MAX_REM_S) { "rem_s" }
        require(remM in 0..MAX_REM_M) { "rem_m" }
    }

    override fun write(w: CanonicalJsonWriter) {
        w.field("m", m)
        w.field("exit", exit.toLong())
        w.field("dist_m", distM)
        w.field("street", street)
        w.field("rem_s", remS)
        w.field("rem_m", remM)
        w.field("flags", flags)
    }

    companion object {
        const val STREET_BYTES = 48
        private const val M_BYTES = 16
        private const val MAX_EXIT = 15
        private const val MAX_DIST_M = 1_000_000L
        private const val MAX_REM_S = 604_800L
        private const val MAX_REM_M = 10_000_000L
        private const val MAX_FLAGS = 3

        fun read(f: Fields) =
            Nav(
                f.text("m"),
                f.int("exit", 0..MAX_EXIT),
                f.long("dist_m", 0..MAX_DIST_M),
                f.text("street"),
                f.long("rem_s", 0..MAX_REM_S),
                f.long("rem_m", 0..MAX_REM_M),
                f.set("flags", MAX_FLAGS),
            )
    }
}

data class NavEnd(
    val reason: NavEndReason,
) : JsonMessage {
    override val type get() = MsgType.NAV_END

    override fun write(w: CanonicalJsonWriter) = w.field("reason", reason.wire)

    companion object {
        fun read(f: Fields) = NavEnd(f.enum("reason"))
    }
}

data class Call(
    val phase: CallPhase,
    val caller: String,
) : JsonMessage {
    override val type get() = MsgType.CALL

    init {
        requireText(caller, CALLER_BYTES, "caller")
    }

    override fun write(w: CanonicalJsonWriter) {
        w.field("phase", phase.wire)
        w.field("caller", caller)
    }

    companion object {
        const val CALLER_BYTES = 32

        fun read(f: Fields) = Call(f.enum("phase"), f.text("caller"))
    }
}

data class Media(
    val avail: Boolean,
    val playing: Boolean,
    val title: String,
    val artist: String,
    val posS: Int,
    val durS: Int,
) : JsonMessage {
    override val type get() = MsgType.MEDIA

    init {
        requireText(title, TEXT_BYTES, "title")
        requireText(artist, TEXT_BYTES, "artist")
        require(posS in 0..MAX_SECONDS && durS in 0..MAX_SECONDS) { "pos_s, dur_s" }
    }

    override fun write(w: CanonicalJsonWriter) {
        w.field("avail", avail)
        w.field("playing", playing)
        w.field("title", title)
        w.field("artist", artist)
        w.field("pos_s", posS.toLong())
        w.field("dur_s", durS.toLong())
    }

    companion object {
        const val TEXT_BYTES = 48
        private const val MAX_SECONDS = 86_400

        fun read(f: Fields) =
            Media(
                f.bool("avail"),
                f.bool("playing"),
                f.text("title"),
                f.text("artist"),
                f.int("pos_s", 0..MAX_SECONDS),
                f.int("dur_s", 0..MAX_SECONDS),
            )
    }
}
