#!/usr/bin/env python3
"""A fake phone for sārathi: talks protocol v1 (docs/protocol/protocol.md, ADR-0007) to a
device running the crowpanel21-phone build, over BLE with bleak.

It pairs (the device shows a 6-digit code; confirm it on the dash with the knob, and on this
computer), then replays the firmware's demo ride: hello, time, battery, ride frame, backend,
route, traffic, then nav at 1 Hz and pose at 5 Hz, an incoming call at 60 s and a playlist.
Knob actions come back as call_action and media_cmd and are honoured. Frames go out within the
device's flow-control window. Everything is encoded by tools/protocol_vectors.py, the same
reference the golden vectors come from.

Commands:
  scan                 list devices advertising the sārathi service
  ride                 pair if needed, then replay the demo ride (Ctrl-C to stop)
  forget               remove the device's pairing from this computer

Pairing on Linux uses BlueZ's default agent: on a desktop, a dialog shows the code. Without a
desktop, pass --agent to confirm the code in this terminal instead. On macOS and Windows the
system shows its own dialog.

Run from the project .venv:  python tools/ble_client.py ride --backend osm
"""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import datetime
import math
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import protocol_vectors as pv  # noqa: E402  (the reference encoder, next to this file)

from bleak import BleakClient, BleakScanner  # noqa: E402

SERVICE_UUID = "831fd13f-67ea-432e-b7b7-09926c4b92a6"
RX_UUID = "cd32526c-d882-496f-b7c1-301a1fdf68a7"  # phone -> device
TX_UUID = "9ae416fd-2b66-43f5-b902-001a23a238a9"  # device -> phone
MIN_MTU = 247
ATT_HEADER = 3
APP_VERSION = "0.0.0-fake"

# Maneuver codes (protocol.md section 3, "Maneuvers").
MANEUVERS = ["none", "depart", "straight", "slight_left", "left", "sharp_left", "slight_right",
             "right", "sharp_right", "uturn", "roundabout", "arrive", "keep_left", "keep_right",
             "merge", "fork_left", "fork_right", "ramp_left", "ramp_right", "ferry"]

# The demo ride, copied from firmware/lib/sarathi_core/demo_ride_data.cpp: metres east and
# north of the start. Keep the two in step.
POINTS = [(0, 0), (0, 400), (300, 400), (450, 430), (600, 500), (700, 600), (700, 1000),
          (700, 1400), (900, 1700), (900, 2300)]
TURNS = [(1, "right", 0, "Station Road"), (5, "left", 0, "Lake View Drive"),
         (6, "roundabout", 2, "Hill Street"), (7, "slight_right", 0, "Ring Road"),
         (9, "arrive", 0, "Destination")]
SEGMENT_KMH = [40, 30, 45, 45, 35, 40, 35, 55, 50]
STOPS = {1: 10.0, 6: 5.0}  # vertex: seconds (a traffic light, a roundabout give-way)
TRACKS = [("Open Road", "Demo Artist", 95), ("Night Ride", "Demo Artist", 80),
          ("Long Way Home", "Demo Band", 110)]
CALLER = "Demo Caller"
CALL_AT_S, CALL_MISSED_AFTER_S, CALL_ENDS_AFTER_S = 60.0, 20.0, 25.0
RESTART_AFTER_ARRIVAL_S = 10.0

# Routing backends as the app will report them (ADR-0006). Text is ASCII: the device's fonts
# have no "©" (protocol.md section 3).
BACKENDS = {
    "google": {"routing": "google", "search": "google", "name": "Google",
               "attribution": "Map data (c) Google", "traffic": True, "two_wheeler": True},
    "mappls": {"routing": "mappls", "search": "mappls", "name": "Mappls",
               "attribution": "(c) MapmyIndia", "traffic": True, "two_wheeler": True},
    "osm": {"routing": "osm_online", "search": "osm", "name": "OSM",
            "attribution": "(c) OpenStreetMap contributors", "traffic": False,
            "two_wheeler": True},
    "offline": {"routing": "osm_offline", "search": "osm", "name": "OSM (offline)",
                "attribution": "(c) OpenStreetMap contributors", "traffic": False,
                "two_wheeler": False},
}


def log(direction: str, text: str) -> None:
    print(f"{time.strftime('%H:%M:%S')} {direction} {text}", flush=True)


# ---- The link: framing, flow control, decoding ----

@dataclass
class Link:
    client: BleakClient
    mtu: int
    on_message: callable
    msg_id: int = 0
    sent: int = 0  # frames since the connection started
    consumed: int = 0
    window: int = 0  # 0 until the device's first flow: only hello may go
    credit: asyncio.Event = field(default_factory=asyncio.Event)
    reassembler: pv.Reassembler = field(default_factory=pv.Reassembler)

    async def send(self, name: str, payload: bytes, *, response: bool = False) -> None:
        frames = pv.fragment(pv.CODES[name], self.msg_id & 0xFF, payload, self.mtu)
        self.msg_id += 1
        for frame in frames:
            while name != "hello" and (self.sent - self.consumed) % (1 << 32) >= self.window:
                self.credit.clear()
                await self.credit.wait()
            await self.client.write_gatt_char(RX_UUID, frame, response=response)
            self.sent += 1

    async def json(self, obj: dict, *, response: bool = False) -> None:
        await self.send(obj["t"], pv.canonical_json(obj["t"], "phone", obj), response=response)

    async def binary(self, name: str, message: dict) -> None:
        await self.send(name, pv.encode_binary(name, message))

    def notified(self, _sender, data: bytearray) -> None:
        for event in self.reassembler.feed(bytes(data)):
            if event[0] == "drop":
                log("!", f"frame dropped: {event[1]}")
                continue
            _, msg_type, _, payload = event
            name = pv.TYPES.get(msg_type, f"0x{msg_type:02X}")
            try:
                message = (pv.decode_binary(name, payload) if name in pv.BINARY
                           else pv.validate_json(msg_type, payload, "device"))
            except pv.Invalid as e:
                log("!", f"{name} dropped: {e}")
                continue
            if name == "flow":
                self.window, self.consumed = message["window"], message["consumed"]
                self.credit.set()
            self.on_message(name, message)


# ---- The demo ride ----

def segment_lengths() -> list[float]:
    return [math.dist(POINTS[i], POINTS[i + 1]) for i in range(len(POINTS) - 1)]


class Ride:
    """Moves along the demo route in ride time, like DemoRide in the firmware."""

    def __init__(self) -> None:
        self.lengths = segment_lengths()
        self.cumulative = [0.0]
        for length in self.lengths:
            self.cumulative.append(self.cumulative[-1] + length)
        self.restart()

    def restart(self) -> None:
        self.t = 0.0
        self.s = 0.0
        self.stop_left = 0.0
        self.stops_done: set[int] = set()
        self.arrived_at: float | None = None

    @property
    def length(self) -> float:
        return self.cumulative[-1]

    def segment(self) -> int:
        seg = 0
        while seg + 2 < len(POINTS) and self.cumulative[seg + 1] <= self.s:
            seg += 1
        return seg

    def speed_mps(self) -> float:
        if self.arrived_at is not None or self.stop_left > 0:
            return 0.0
        return SEGMENT_KMH[self.segment()] / 3.6

    def advance(self, dt: float) -> bool:
        """Returns True when the ride restarts."""
        self.t += dt
        if self.arrived_at is not None:
            if self.t - self.arrived_at >= RESTART_AFTER_ARRIVAL_S:
                self.restart()
                return True
            return False
        if self.stop_left > 0:
            wait = min(self.stop_left, dt)
            self.stop_left -= wait
            dt -= wait
        nxt = self.s + self.speed_mps() * dt
        for vertex, seconds in STOPS.items():
            at = self.cumulative[vertex]
            if vertex not in self.stops_done and self.s < at <= nxt:
                nxt, self.stop_left = at, seconds
                self.stops_done.add(vertex)
                break
        self.s = nxt
        if self.s >= self.length:
            self.s, self.arrived_at = self.length, self.t
        return False

    def position(self) -> tuple[float, float, float]:
        """x, y in metres and the heading in degrees."""
        seg = self.segment()
        (ax, ay), (bx, by) = POINTS[seg], POINTS[seg + 1]
        t = (self.s - self.cumulative[seg]) / self.lengths[seg] if self.lengths[seg] else 0.0
        heading = math.degrees(math.atan2(bx - ax, by - ay)) % 360
        return ax + (bx - ax) * t, ay + (by - ay) * t, heading

    def nav(self) -> dict:
        nxt = next((turn for turn in TURNS if self.cumulative[turn[0]] > self.s), None)
        if nxt is None:  # at the destination
            return {"t": "nav", "m": "arrive", "exit": 0, "dist_m": 0, "street": "",
                    "rem_s": 0, "rem_m": 0, "flags": ["arrived"]}
        vertex, maneuver, exit_number, street = nxt
        remaining = self.length - self.s
        return {"t": "nav", "m": maneuver, "exit": exit_number,
                "dist_m": round(self.cumulative[vertex] - self.s), "street": street,
                "rem_s": round(remaining / (40 / 3.6)), "rem_m": round(remaining), "flags": []}


def route_message(route_id: int, version: int, backend: str, epoch: int) -> dict:
    return {"route_id": route_id, "version": version,
            "backend": ["google", "mappls", "osm_online", "osm_offline"].index(
                BACKENDS[backend]["routing"]),
            "epoch": epoch, "mode": 0, "quantum_dm": 10,
            "points": [[x, y] for x, y in POINTS],
            "markers": [[v, MANEUVERS.index(m), e] for v, m, e, _ in TURNS]}


def traffic_message(route_id: int, version: int) -> dict:
    # A slow stretch on Lake View Drive and a jam before the roundabout, the rest free.
    return {"route_id": route_id, "version": version,
            "entries": [[0, 700, 0], [700, 250, 1], [950, 200, 2], [1150, 1500, 0]]}


def local_time() -> dict:
    now = datetime.datetime.now().astimezone()
    offset = now.utcoffset() or datetime.timedelta(0)
    return {"t": "time", "local": now.strftime("%Y-%m-%dT%H:%M:%S"),
            "utc_offset_min": int(offset.total_seconds() // 60)}


class Phone:
    """The phone's side of a ride: what to send when, and what to do with knob actions."""

    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.ride = Ride()
        self.backend = args.backend
        self.route_id = 1
        self.epoch = 1
        self.device_hello: dict | None = None
        self.hello_seen = asyncio.Event()
        self.call = "idle"
        self.call_sent = "idle"  # the phase the device last heard
        self.call_since = 0.0
        self.call_done = False
        self.track, self.track_pos, self.playing = 0, 0.0, True
        self.media_dirty = True
        self.media_sent_at = -10.0  # ride time
        self.nav_shown = ("", "")  # (maneuver, street) last logged
        self.link: Link | None = None

    def on_message(self, name: str, m: dict) -> None:
        if name == "hello":
            self.device_hello = m
            log("<-", f"hello: firmware {m['fw']}, protocol {m['proto'][0]}.{m['proto'][1]}, "
                f"route {m['route']}, epoch {m['epoch']}")
            self.hello_seen.set()
        elif name == "flow":
            pass
        elif name == "style":
            log("<-", f"style: map {m['map']}")
        elif name == "call_action":
            log("<-", f"call_action: {m['action']}")
            if self.call == "ringing" and m["action"] == "accept":
                self.call, self.call_since = "active", self.ride.t
            elif self.call in ("ringing", "active"):
                self.call, self.call_done = "idle", True
        elif name == "media_cmd":
            log("<-", f"media_cmd: {m['cmd']}")
            if m["cmd"] == "play_pause":
                self.playing = not self.playing
            else:
                step = 1 if m["cmd"] == "next" else -1
                self.track, self.track_pos = (self.track + step) % len(TRACKS), 0.0
            self.media_dirty = True
        else:
            log("<-", f"{name}: {m}")

    async def start(self, link: Link) -> None:
        self.link = link
        await link.json({"t": "hello", "proto": [1, 0], "app": APP_VERSION,
                         "caps": ["google", "mappls", "osm", "offline", "traffic", "calls",
                                  "media"]}, response=True)
        log("->", "hello")
        await asyncio.wait_for(self.hello_seen.wait(), 10)
        while link.window == 0:  # the first flow follows the device's hello
            link.credit.clear()
            await asyncio.wait_for(link.credit.wait(), 10)
        await link.json(local_time())
        await link.json({"t": "phone_batt", "level": 64, "charging": False})
        await self.start_ride(resend_route=True)

    async def start_ride(self, *, resend_route: bool) -> None:
        link = self.link
        held = self.device_hello or {}
        if held.get("epoch") != self.epoch:
            await link.json({"t": "frame", "epoch": self.epoch})
            held["epoch"] = self.epoch  # the device holds it now
        await link.json({"t": "backend", **BACKENDS[self.backend]})
        if resend_route or held.get("route") != {"id": self.route_id, "v": 0}:
            await link.binary("route", route_message(self.route_id, 0, self.backend, self.epoch))
            await link.binary("traffic", traffic_message(self.route_id, 0))
        log("->", f"ride start: frame {self.epoch}, backend {BACKENDS[self.backend]['name']}, "
            f"route {self.route_id}")

    async def run(self) -> None:
        link = self.link
        tick, last_nav, last_time = 0.2, -1.0, time.monotonic()
        fallback_done = False
        while True:
            await asyncio.sleep(tick)
            dt = tick * self.args.speed
            if self.ride.advance(dt):
                await link.json({"t": "nav_end", "reason": "arrived"})
                log("->", "nav_end (arrived); the ride starts again")
                self.route_id += 1
                self.call, self.call_sent, self.call_done = "idle", "idle", False
                await self.start_ride(resend_route=True)
                last_nav = -1.0
            t = self.ride.t
            x, y, heading = self.ride.position()
            speed = self.ride.speed_mps()
            await link.binary("pose", {
                "epoch": self.epoch, "t_ms": int(time.monotonic() * 1000) & 0xFFFFFFFF,
                "x_dm": round(x * 10), "y_dm": round(y * 10),
                "heading_cdeg": round(heading * 100) % 36000,
                "speed_cms": min(round(speed * 100), 0xFFFF), "acc_m": 5})
            if t - last_nav >= 1.0 or last_nav < 0:
                last_nav = t
                nav = self.ride.nav()
                await link.json(nav)
                if (nav["m"], nav["street"]) != self.nav_shown:
                    self.nav_shown = (nav["m"], nav["street"])
                    log("->", f"nav {nav['m']} in {nav['dist_m']} m, {nav['street']!r}")
            if (self.args.fallback_at is not None and not fallback_done
                    and t >= self.args.fallback_at):
                fallback_done = True
                before = BACKENDS[self.backend]["routing"]
                self.backend = "mappls" if self.backend != "mappls" else "osm"
                await link.json({"t": "event", "kind": "fallback", "from": before,
                                 "to": BACKENDS[self.backend]["routing"], "reason": "quota"})
                await link.json({"t": "backend", **BACKENDS[self.backend]})
                log("->", f"fallback to {BACKENDS[self.backend]['name']}")
            await self.update_call(t)
            await self.update_media(dt, t)
            if time.monotonic() - last_time >= 600:
                last_time = time.monotonic()
                await link.json(local_time())

    async def update_call(self, t: float) -> None:
        """Rings at 60 s; a knob accept (on_message) makes it active. Sends every change."""
        if self.call == "idle" and not self.call_done and t >= CALL_AT_S:
            self.call, self.call_since = "ringing", t
        elif self.call == "ringing" and t - self.call_since >= CALL_MISSED_AFTER_S:
            self.call, self.call_done = "idle", True
        elif self.call == "active" and t - self.call_since >= CALL_ENDS_AFTER_S:
            self.call, self.call_done = "idle", True
        if self.call != self.call_sent:
            self.call_sent = self.call
            log("->", f"call {self.call}")
            await self.link.json({"t": "call", "phase": self.call,
                                  "caller": CALLER if self.call != "idle" else ""})

    async def update_media(self, dt: float, t: float) -> None:
        """On change, and every 5 s while playing (protocol.md section 3)."""
        if self.playing:
            self.track_pos += dt
            if self.track_pos >= TRACKS[self.track][2]:
                self.track, self.track_pos = (self.track + 1) % len(TRACKS), 0.0
                self.media_dirty = True
        if self.media_dirty or (self.playing and t - self.media_sent_at >= 5.0):
            self.media_sent_at = t
            title, artist, duration = TRACKS[self.track]
            await self.link.json({"t": "media", "avail": True, "playing": self.playing,
                                  "title": title, "artist": artist,
                                  "pos_s": int(self.track_pos), "dur_s": duration})
            if self.media_dirty:
                log("->", f"media {'playing' if self.playing else 'paused'}: {title}")
            self.media_dirty = False


# ---- BlueZ pairing agent (--agent, Linux only) ----

@contextlib.asynccontextmanager
async def terminal_agent():
    """Registers a BlueZ agent that shows the pairing code here and asks to confirm it. It
    becomes the default agent while the tool runs; BlueZ returns to the previous default
    (the desktop's) when it unregisters."""
    from dbus_fast import BusType, DBusError
    from dbus_fast.aio import MessageBus
    from dbus_fast.service import ServiceInterface, method

    class Agent(ServiceInterface):
        def __init__(self) -> None:
            super().__init__("org.bluez.Agent1")

        @method()
        async def RequestConfirmation(self, device: "o", passkey: "u"):  # noqa: N802, F821
            loop = asyncio.get_running_loop()
            answer = await loop.run_in_executor(
                None, input, f"\nPairing code {passkey:06d}: is it the code on the dash? [y/N] ")
            if answer.strip().lower() not in ("y", "yes"):
                raise DBusError("org.bluez.Error.Rejected", "the codes don't match")

        @method()
        def Release(self):  # noqa: N802
            pass

        @method()
        def Cancel(self):  # noqa: N802
            print("pairing cancelled")

    path = "/io/github/d4rthc0d3r/sarathi/agent"
    bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
    bus.export(path, Agent())
    introspection = await bus.introspect("org.bluez", "/org/bluez")
    manager = bus.get_proxy_object("org.bluez", "/org/bluez", introspection).get_interface(
        "org.bluez.AgentManager1")
    await manager.call_register_agent(path, "DisplayYesNo")
    await manager.call_request_default_agent(path)
    try:
        yield
    finally:
        with contextlib.suppress(Exception):
            await manager.call_unregister_agent(path)
        bus.disconnect()


# ---- Commands ----

async def find(args: argparse.Namespace):
    if args.address:
        device = await BleakScanner.find_device_by_address(args.address, timeout=10)
    else:
        device = await BleakScanner.find_device_by_filter(
            lambda d, adv: SERVICE_UUID in [u.lower() for u in adv.service_uuids], timeout=10)
    if device is None:
        sys.exit("no sārathi device found: is it running the crowpanel21-phone build, with BLE "
                 "not paused for Wi-Fi?")
    return device


async def cmd_scan(_args: argparse.Namespace) -> None:
    found = await BleakScanner.discover(timeout=8, return_adv=True)
    hits = [(d, adv) for d, adv in found.values()
            if SERVICE_UUID in [u.lower() for u in adv.service_uuids]]
    for d, adv in hits:
        print(f"{d.address}  {adv.local_name or d.name}  RSSI {adv.rssi}")
    if not hits:
        print("none found")


async def negotiated_mtu(client: BleakClient) -> int:
    backend = getattr(client, "_backend", None)
    acquire = getattr(backend, "_acquire_mtu", None)  # BlueZ: needs a call before mtu_size
    if acquire is not None:
        with contextlib.suppress(Exception):
            await acquire()
    return client.mtu_size


async def cmd_ride(args: argparse.Namespace) -> None:
    device = await find(args)
    log("..", f"connecting to {device.address}")
    agent = terminal_agent() if args.agent else contextlib.nullcontext()
    log("..", "pairing if needed: compare the codes, then Pair on the dash with the knob")
    # Pair before service discovery (BlueZ, Windows), so the system's pairing agent runs it;
    # macOS pairs on the first secured access instead.
    async with agent, BleakClient(device, timeout=60, pair=sys.platform != "darwin") as client:
        mtu = 0
        for _ in range(20):  # the device starts the exchange; give it a moment
            mtu = await negotiated_mtu(client)
            if mtu >= MIN_MTU:
                break
            await asyncio.sleep(0.25)
        if mtu < MIN_MTU:
            sys.exit(f"MTU {mtu} is below {MIN_MTU}; the device won't use this link")
        mtu = min(mtu, 517)
        log("..", f"secured, MTU {mtu}")
        phone = Phone(args)
        link = Link(client, mtu, phone.on_message)
        await client.start_notify(TX_UUID, link.notified)
        await phone.start(link)
        await phone.run()


async def cmd_forget(args: argparse.Namespace) -> None:
    device = await find(args)
    # No connection needed (and a stale pairing would refuse one): the system just drops it.
    await BleakClient(device).unpair()
    print(f"forgot {device.address} on this computer; use Settings > Forget phones on the dash "
          "to forget this computer there")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["scan", "ride", "forget"])
    parser.add_argument("--address", help="the device's address (default: the first found)")
    parser.add_argument("--backend", choices=sorted(BACKENDS), default="google",
                        help="the routing backend to report (default: google)")
    parser.add_argument("--speed", type=float, default=1.0,
                        help="replay speed, 1 = real time (default 1)")
    parser.add_argument("--fallback-at", type=float, metavar="SECONDS",
                        help="switch backend (a fallback) at this ride time")
    parser.add_argument("--agent", action="store_true",
                        help="Linux: confirm the pairing code in this terminal")
    args = parser.parse_args()
    command = {"scan": cmd_scan, "ride": cmd_ride, "forget": cmd_forget}[args.command]
    try:
        asyncio.run(command(args))
    except KeyboardInterrupt:
        print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
