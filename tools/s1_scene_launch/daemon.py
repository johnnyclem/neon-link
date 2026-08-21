#!/usr/bin/env python3
"""S1 Phase A — scene launch observability daemon.

Observes AbletonOSC and writes one CSV row per event. No hardware, no
clients, no audio. The output is a CSV; the result is the lookahead
distribution computed by analyze.py.

OSC addresses are taken from the AbletonOSC README, not guessed:
  listen:  /live/<obj>/start_listen/<prop> [args...]
  replies: /live/<obj>/get/<prop> — same address for get replies and
           listener pushes; they are indistinguishable on the wire.
AbletonOSC listens on 11000 and replies to the sender's IP on 11001,
so this process must own UDP 11001. If something else on the machine
already speaks OSC on 11001, close it first.

Run on the same machine as Live. The point of Phase A is the margin
Live itself gives us; measuring it through a WiFi hop adds the network
tail to every number and answers a different question.
"""

import argparse
import csv
import math
import sys
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime
from typing import Optional

try:
    from pythonosc.dispatcher import Dispatcher
    from pythonosc.osc_server import ThreadingOSCUDPServer
    from pythonosc.udp_client import SimpleUDPClient
except ImportError:
    sys.exit("python-osc is required: pip install python-osc")

# Song.clip_trigger_quantization enum -> quantum.
# ("bars", n) scales with the time signature; ("beats", x) is a fixed note
# value in Live beat-time (quarter notes). Triplet entries: 3 in the space
# of 2, hence the thirds.
SONG_Q = {
    0: None,             # None — undetectable ahead of time by definition
    1: ("bars", 8),
    2: ("bars", 4),
    3: ("bars", 2),
    4: ("bars", 1),
    5: ("beats", 2.0),        # 1/2
    6: ("beats", 4.0 / 3.0),  # 1/2T
    7: ("beats", 1.0),        # 1/4
    8: ("beats", 2.0 / 3.0),  # 1/4T
    9: ("beats", 0.5),        # 1/8
    10: ("beats", 1.0 / 3.0),  # 1/8T
    11: ("beats", 0.25),       # 1/16
    12: ("beats", 1.0 / 6.0),  # 1/16T
    13: ("beats", 0.125),      # 1/32
}

# Clip.launch_quantization: 0 = use global, 1 = none, n>=2 = SONG_Q[n-1].
CLIP_Q_GLOBAL = 0
CLIP_Q_NONE = 1

CSV_COLUMNS = [
    "t_wall", "t_mono_ms", "event", "track", "scene", "bpm",
    "global_q", "clip_q", "quantum_beats",
    "now_beats", "predicted_target_beat", "lookahead_ms",
    "actual_start_beat", "prediction_error_beats", "notes",
]

# How long to wait for the per-clip launch_quantization reply before
# writing the fired row with the global quantum. The prediction snapshot
# is taken at fired-event receipt either way; this only delays the write.
CLIP_Q_TIMEOUT_S = 0.3
# How long an unmatched fired prediction is kept for pairing with its
# playing event (8 bars at 40 BPM is 48 s).
PENDING_MATCH_TTL_S = 60.0


@dataclass
class PendingFired:
    track: int
    scene: int
    t_mono: float
    t_wall: str
    now_beats: Optional[float]
    bpm: Optional[float]
    global_q: Optional[int]
    beats_per_bar: float
    notes: list = field(default_factory=list)
    clip_q: Optional[int] = None
    clip_q_received: bool = False
    written: bool = False
    # Filled at write time, kept for pairing with the playing event:
    predicted: Optional[float] = None
    quantum_beats: Optional[float] = None
    lookahead_ms: Optional[float] = None


class Daemon:
    def __init__(self, args):
        self.args = args
        self.client = SimpleUDPClient(args.host, args.send_port)
        self.lock = threading.RLock()

        # Cached song state
        self.tempo: Optional[float] = None
        self.global_q: Optional[int] = None
        self.is_playing = False
        self.sig_num = 4
        self.sig_den = 4
        self.song_time: Optional[float] = None   # last observed beats
        self.song_time_mono: Optional[float] = None
        self.num_tracks = 0
        self.num_scenes = 0

        self.pending: dict = {}   # (track, scene) -> PendingFired, awaiting clip_q
        self.fired_seen: dict = {}  # (track, scene) -> PendingFired, awaiting playing
        self.last_fired: dict = {}  # track -> last fired_slot_index value

        self.csv_file = open(args.out, "w", newline="")
        self.csv = csv.DictWriter(self.csv_file, fieldnames=CSV_COLUMNS)
        self.csv.writeheader()
        self.csv_file.flush()
        print(f"writing {args.out}")

    # ---- time ----------------------------------------------------------

    def mono(self):
        return time.monotonic()

    def wall(self):
        return datetime.now().isoformat(timespec="milliseconds")

    def beats_per_bar(self):
        return self.sig_num * 4.0 / self.sig_den

    def now_beats_estimate(self):
        """Best estimate of current_song_time right now.

        The listener push arrives at UI rate (~60-100 ms); while the
        transport runs we extrapolate from the last push using the tempo.
        Stopped transport does not advance, so no extrapolation then.
        """
        if self.song_time is None:
            return None
        if not self.is_playing or self.tempo is None:
            return self.song_time
        return self.song_time + (self.mono() - self.song_time_mono) * self.tempo / 60.0

    # ---- csv -----------------------------------------------------------

    def row(self, event, **kw):
        r = {c: "" for c in CSV_COLUMNS}
        r["t_wall"] = kw.pop("t_wall", self.wall())
        r["t_mono_ms"] = kw.pop("t_mono_ms", f"{self.mono() * 1000:.1f}")
        r["event"] = event
        for k, v in kw.items():
            if v is None:
                continue
            r[k] = f"{v:.4f}" if isinstance(v, float) else v
        with self.lock:
            self.csv.writerow(r)
            self.csv_file.flush()

    # ---- quantum / prediction -----------------------------------------

    def quantum_to_beats(self, song_q_value):
        q = SONG_Q.get(song_q_value)
        if q is None:
            return None
        kind, n = q
        return n * self.beats_per_bar() if kind == "bars" else n

    def effective_quantum(self, clip_q):
        """Returns (quantum_beats or None, note). clip_q may be None
        (reply never came), CLIP_Q_GLOBAL, CLIP_Q_NONE, or >= 2."""
        if clip_q is None or clip_q == CLIP_Q_GLOBAL:
            src = "global_q" if clip_q == CLIP_Q_GLOBAL else "global_q_assumed"
            if self.global_q is None:
                return None, "no_global_q_cached"
            return self.quantum_to_beats(self.global_q), src
        if clip_q == CLIP_Q_NONE:
            return None, "clip_q_none"
        return self.quantum_to_beats(clip_q - 1), "clip_q"

    def predict(self, now_beats, quantum):
        """Next grid point strictly after now. ceil() per the plan, but an
        exactly-on-grid click targets the *next* line, matching Live."""
        target = math.ceil(now_beats / quantum) * quantum
        if target <= now_beats + 1e-9:
            target += quantum
        return target

    # ---- OSC handlers --------------------------------------------------

    def on_song_time(self, addr, *a):
        with self.lock:
            self.song_time = float(a[0])
            self.song_time_mono = self.mono()

    def on_tempo(self, addr, *a):
        with self.lock:
            changed = self.tempo is not None and self.tempo != float(a[0])
            self.tempo = float(a[0])
        if changed:
            self.row("tempo", bpm=self.tempo)

    def on_is_playing(self, addr, *a):
        with self.lock:
            self.is_playing = bool(a[0])
            # A stale beat cache from the last run must not be extrapolated.
            if self.is_playing:
                self.song_time_mono = self.mono()
        self.row("is_playing", now_beats=self.now_beats_estimate(),
                 notes=f"is_playing={int(self.is_playing)}")

    def on_global_q(self, addr, *a):
        with self.lock:
            changed = self.global_q is not None and self.global_q != int(a[0])
            self.global_q = int(a[0])
        if changed:
            self.row("global_q_changed", global_q=self.global_q,
                     quantum_beats=self.quantum_to_beats(self.global_q))

    def on_sig_num(self, addr, *a):
        with self.lock:
            self.sig_num = int(a[0])

    def on_sig_den(self, addr, *a):
        with self.lock:
            self.sig_den = int(a[0])

    def on_fired(self, addr, *a):
        track, index = int(a[0]), int(a[1])
        t_mono, t_wall = self.mono(), self.wall()
        with self.lock:
            prev = self.last_fired.get(track)
            self.last_fired[track] = index
        if index < 0:
            # -1 = cleared (normally the moment playback starts),
            # -2 = stop button fired.
            self.row("fired_cleared", track=track, scene=prev,
                     notes=f"fired_slot_index={index}")
            return
        p = PendingFired(
            track=track, scene=index, t_mono=t_mono, t_wall=t_wall,
            now_beats=self.now_beats_estimate(), bpm=self.tempo,
            global_q=self.global_q, beats_per_bar=self.beats_per_bar(),
        )
        if not self.is_playing:
            p.notes.append("transport_stopped")
        with self.lock:
            self.pending[(track, index)] = p
        # Refresh the beat cache and fetch the per-clip override. The
        # prediction snapshot is already taken; these do not move it.
        self.client.send_message("/live/song/get/current_song_time", [])
        self.client.send_message("/live/clip/get/launch_quantization",
                                 [track, index])

    def on_clip_q(self, addr, *a):
        track, scene, value = int(a[0]), int(a[1]), int(a[2])
        with self.lock:
            p = self.pending.get((track, scene))
            if p is not None and not p.clip_q_received:
                p.clip_q = value
                p.clip_q_received = True
        if p is not None:
            self.write_fired(p)

    def write_fired(self, p: PendingFired):
        with self.lock:
            if p.written:
                return
            p.written = True
            self.pending.pop((p.track, p.scene), None)

        quantum, qnote = self.effective_quantum(p.clip_q)
        p.quantum_beats = quantum
        notes = p.notes + [f"quantum_from={qnote}"]
        if not p.clip_q_received:
            notes.append("clip_q_timeout")

        if p.now_beats is None:
            notes.append("no_song_time")
        elif quantum is None:
            notes.append("quantization_none")
        else:
            p.predicted = self.predict(p.now_beats, quantum)
            if p.bpm:
                p.lookahead_ms = (p.predicted - p.now_beats) * 60000.0 / p.bpm

        with self.lock:
            self.fired_seen[(p.track, p.scene)] = p

        self.row("fired", t_wall=p.t_wall, t_mono_ms=f"{p.t_mono * 1000:.1f}",
                 track=p.track, scene=p.scene, bpm=p.bpm,
                 global_q=p.global_q, clip_q=p.clip_q,
                 quantum_beats=quantum, now_beats=p.now_beats,
                 predicted_target_beat=p.predicted,
                 lookahead_ms=p.lookahead_ms, notes=";".join(notes))
        if p.lookahead_ms is not None:
            print(f"fired  t{p.track} s{p.scene}  lookahead {p.lookahead_ms:7.1f} ms"
                  f"  target beat {p.predicted:.3f}")

    def on_playing(self, addr, *a):
        track, index = int(a[0]), int(a[1])
        actual = self.now_beats_estimate()
        if index < 0:
            self.row("playing_cleared", track=track,
                     notes=f"playing_slot_index={index}")
            return
        with self.lock:
            p = self.fired_seen.pop((track, index), None)
        err = None
        notes = []
        if p is None:
            # A10 territory: a launch with no observed fired event
            # (Follow Action, or the listener simply never led playback).
            notes.append("no_pending_fired")
        else:
            notes.append("matched_fired")
            if p.predicted is not None and actual is not None:
                # Raw estimator: beats at *receipt* of the playing event,
                # so delivery latency shows up as a small positive error.
                # analyze.py rounds to the quantum grid for P2.
                err = actual - p.predicted
        self.row("playing", track=track, scene=index,
                 bpm=self.tempo,
                 quantum_beats=p.quantum_beats if p else None,
                 predicted_target_beat=p.predicted if p else None,
                 lookahead_ms=p.lookahead_ms if p else None,
                 actual_start_beat=actual, prediction_error_beats=err,
                 notes=";".join(notes))
        if err is not None:
            print(f"play   t{track} s{index}  observed {actual:.3f}"
                  f"  err {err:+.3f} beats (raw, pre-grid)")

    def on_slot_triggered(self, addr, *a):
        track, scene, val = int(a[0]), int(a[1]), int(a[2])
        self.row("slot_triggered", track=track, scene=scene,
                 now_beats=self.now_beats_estimate(),
                 notes=f"is_triggered={val}")

    def on_startup(self, addr, *a):
        # Live reloaded the control surface; every listener is gone.
        self.row("live_startup", notes="resubscribing")
        print("Live restarted AbletonOSC — resubscribing")
        self.subscribe()

    def on_version(self, addr, *a):
        self.row("meta", notes="live_version=" + ".".join(str(x) for x in a))

    def on_num_tracks(self, addr, *a):
        with self.lock:
            self.num_tracks = int(a[0])
        self.subscribe_tracks()

    def on_num_scenes(self, addr, *a):
        with self.lock:
            self.num_scenes = int(a[0])
        self.subscribe_tracks()

    # ---- subscription --------------------------------------------------

    def subscribe(self):
        send = self.client.send_message
        for prop in ("current_song_time", "tempo", "is_playing",
                     "clip_trigger_quantization",
                     "signature_numerator", "signature_denominator"):
            send(f"/live/song/start_listen/{prop}", [])
            send(f"/live/song/get/{prop}", [])
        send("/live/application/get/version", [])
        send("/live/song/get/num_tracks", [])
        send("/live/song/get/num_scenes", [])

    def subscribe_tracks(self):
        with self.lock:
            n_tracks = min(self.num_tracks, self.args.max_tracks)
            n_scenes = min(self.num_scenes, self.args.max_scenes)
            if n_tracks == 0:
                return
        send = self.client.send_message
        for t in range(n_tracks):
            send("/live/track/start_listen/fired_slot_index", [t])
            send("/live/track/start_listen/playing_slot_index", [t])
        if not self.args.no_slot_listeners and n_scenes:
            for t in range(n_tracks):
                for s in range(n_scenes):
                    send("/live/clip_slot/start_listen/is_triggered", [t, s])
        print(f"listening on {n_tracks} tracks × {n_scenes} scenes")

    # ---- timeout sweep -------------------------------------------------

    def sweep(self):
        while True:
            time.sleep(0.05)
            now = self.mono()
            with self.lock:
                stale = [p for p in self.pending.values()
                         if now - p.t_mono > CLIP_Q_TIMEOUT_S]
                dead = [k for k, p in self.fired_seen.items()
                        if now - p.t_mono > PENDING_MATCH_TTL_S]
                for k in dead:
                    del self.fired_seen[k]
            for p in stale:
                self.write_fired(p)

    # ---- main ----------------------------------------------------------

    def run(self):
        d = Dispatcher()
        d.map("/live/song/get/current_song_time", self.on_song_time)
        d.map("/live/song/get/tempo", self.on_tempo)
        d.map("/live/song/get/is_playing", self.on_is_playing)
        d.map("/live/song/get/clip_trigger_quantization", self.on_global_q)
        d.map("/live/song/get/signature_numerator", self.on_sig_num)
        d.map("/live/song/get/signature_denominator", self.on_sig_den)
        d.map("/live/song/get/num_tracks", self.on_num_tracks)
        d.map("/live/song/get/num_scenes", self.on_num_scenes)
        d.map("/live/track/get/fired_slot_index", self.on_fired)
        d.map("/live/track/get/playing_slot_index", self.on_playing)
        d.map("/live/clip_slot/get/is_triggered", self.on_slot_triggered)
        d.map("/live/clip/get/launch_quantization", self.on_clip_q)
        d.map("/live/startup", self.on_startup)
        d.map("/live/application/get/version", self.on_version)

        try:
            server = ThreadingOSCUDPServer(
                (self.args.bind, self.args.receive_port), d)
        except OSError as e:
            sys.exit(f"cannot bind UDP {self.args.receive_port}: {e}\n"
                     "AbletonOSC replies to port 11001 — close whatever "
                     "else is holding it.")

        threading.Thread(target=self.sweep, daemon=True).start()
        self.subscribe()
        print(f"→ Live at {self.args.host}:{self.args.send_port}, "
              f"replies on :{self.args.receive_port}. Ctrl-C to stop.")
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            self.unsubscribe()
            self.csv_file.close()
            print(f"\nwrote {self.args.out}")

    def unsubscribe(self):
        send = self.client.send_message
        for prop in ("current_song_time", "tempo", "is_playing",
                     "clip_trigger_quantization",
                     "signature_numerator", "signature_denominator"):
            send(f"/live/song/stop_listen/{prop}", [])
        with self.lock:
            n_tracks = min(self.num_tracks, self.args.max_tracks)
            n_scenes = min(self.num_scenes, self.args.max_scenes)
        for t in range(n_tracks):
            send("/live/track/stop_listen/fired_slot_index", [t])
            send("/live/track/stop_listen/playing_slot_index", [t])
        if not self.args.no_slot_listeners:
            for t in range(n_tracks):
                for s in range(n_scenes):
                    send("/live/clip_slot/stop_listen/is_triggered", [t, s])


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--cell", default="adhoc",
                    help="test-matrix cell label (A1..A10); goes in the filename")
    ap.add_argument("--out", default=None, help="CSV path (default derived from --cell)")
    ap.add_argument("--host", default="127.0.0.1", help="Live's IP")
    ap.add_argument("--send-port", type=int, default=11000)
    ap.add_argument("--receive-port", type=int, default=11001)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--max-tracks", type=int, default=32)
    ap.add_argument("--max-scenes", type=int, default=32)
    ap.add_argument("--no-slot-listeners", action="store_true",
                    help="skip per-slot is_triggered listeners")
    args = ap.parse_args()
    if args.out is None:
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        args.out = f"s1_{args.cell}_{stamp}.csv"
    Daemon(args).run()


if __name__ == "__main__":
    main()
