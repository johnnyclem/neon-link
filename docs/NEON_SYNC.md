# Neon Sync — the Link-free session protocol

**Status:** v0 implemented and host-validated. `components/neon_sync`
(portable core, CI-tested under the host simulator) +
`components/neon_sync_esp` (socket task) ship in-tree; `CONFIG_NEON_SYNC`
selects it in place of Ableton Link. Hardware bring-up and the studio-mode
bench A/B are the remaining validation steps (§7).

## 1. Why

Ableton Link is dual-licensed GPLv2+/commercial (README.md "Licensing
Notes", docs/ARCHITECTURE.md). Because ESP-IDF is Apache-2.0 (incompatible
with GPLv2, compatible with GPLv3), a Link-carrying firmware image is
distributable only under GPLv3 terms — including §6 installation-information
obligations that foreclose locked secure boot on a consumer device, with
copyleft over the whole statically-linked image. Neon Sync is our own
session protocol behind the same seam: no Link code, no copyleft, and room
to beat Link on our own hardware (the WiFi TSF fast path, §4.3).

The wire protocol itself is ours; nothing here speaks or derives from
Link's wire format or source. Neon Sync peers therefore do **not**
interoperate with Link peers: Ableton Live (and every other VST3 host) is
bridged via MIDI clock (`ClockEngine` out, `ExtClockEstimator` in) and via
the VST3 plugin, which is itself a Neon Sync peer that writes the DAW's
`AudioPlayHead` timeline into the mesh (§5); a clean-room Link-compatible
wire module remains a possible later add-on, deliberately off the critical
path.

## 2. The replacement contract

The firmware consumes exactly what `hal::ILinkSession` captures into
`hal::LinkState` every 10 ms, and nothing else:

1. **Peer discovery** — zero-config, leaderless, on the local segment.
2. **Session agreement** — shared tempo + beat grid (phase modulo quantum),
   last-writer-wins, any peer may write (docs/NEARBY.md §5.3).
3. **Start/stop intent** — shared transport, applied on a quantum boundary.
4. **Clock-offset estimation** — good enough to hold the studio-mode phase
   envelope (docs/STUDIO_MODE_RESULTS.md).

`nsync::Node` implements all four as a sans-I/O state machine: packets and
timestamps in, packets out through an `Emitter`. The ESP glue
(`neon_sync_esp`) is the fourth implementation of the `ILinkSession` seam,
next to real Link, the stub, and the Daisy internal timeline — every
target, UI, and test downstream of `neon::TimelineSnapshot` is unchanged.

## 3. Wire format

Fixed-layout little-endian messages (`nsync/wire.hpp`), each under one MTU,
on our own multicast group **239.77.83.78:20809** (unicast to the sender's
address for measurement traffic). Header: magic `NSYN`, protocol version,
message type, node id, session id.

- `ANNOUNCE` (multicast, ~1 s jittered ±200 ms, plus immediately on any
  state write or adoption): the full `SessionState`, the sender's ghost
  offset (§4.2), the sender's local clock at transmit (coarse offset seed),
  and a TTL after which the sender may be presumed gone (default 3.5 s).
- `BYE` (multicast): clean shutdown; receivers drop the peer immediately.
- `PING`/`PONG` (unicast): four-timestamp NTP-style measurement round.
- `TSF_HINT` (multicast, ~2 s): the sender's `(bssid, tsf_us, local_us)`
  sample for the same-BSS fast path.

`SessionState` is deliberately isomorphic to `neon::TimelineSnapshot` —
Q32.32 µs-per-beat, Q32.32 signed beats, milli-beat quantum. No doubles on
the wire; doubles appear only at the capture boundary, exactly as with
Link (`neon::build_snapshot`).

Timeline and transport are versioned **independently** so a tempo edit and
a start/stop can never clobber each other:

```
TimelineState:  seq, writer, origin_session_us, beat_at_origin_q32,
                tempo_mpb_q32, quantum_mb
TransportState: seq, writer, toggle_session_us, playing
```

## 4. How it works

### 4.1 Agreement — leaderless last-writer-wins

`seq` is a Lamport counter: a writer sets `seq = max(seen) + 1`, ties break
on the higher `writer` id. Any peer receiving a winning state adopts it and
re-announces (gossip; one multicast hop on a single segment). The highest
node id founds a session's id and the id travels with adopted state, so two
islands merging converge on the island holding the most recent human action
— the behavior users already expect from Link. Merging two never-edited
islands (both at seq 1) deterministically adopts the higher writer id's
grid; one side necessarily jumps, as in any merge.

### 4.2 Session time and the ghost offset

Every peer keeps `ghost = session_us − local_us`. The founder pins ghost at
0 (session time *is* its clock); every announce carries the sender's
current ghost, so a receiver derives its own mapping as
`sender_ghost + offset(sender)` from any peer it can measure. The
**reference** for steady-state discipline is the highest node id among live
peers and self — deterministic from membership, no election traffic.
Followers run a PI servo on their ghost (Q48.16): the proportional term
low-passes estimator noise, the integral term learns relative crystal
drift, and the total step is slew-bounded (default 1000 µs/s) so the beat
grid always moves smoothly under the pulse engine. Adopting a state from a
*different* session jumps the ghost once to that session's time domain
(sender ghost + offset), which also keeps a high-id newcomer from dragging
an established session onto its own clock.

### 4.3 Clock offsets — two estimators per peer (`nsync/peer_clock.hpp`)

- **Measured path:** PING/PONG rounds (burst at join, 500 ms warmup, then
  every 500 ms). A 64-deep window of `(offset, rtt)` samples; the estimate
  is the median of the lowest-effective-RTT third, where effective RTT
  charges 200 µs per second of sample age (a stale lucky sample is worth
  less than a fresh decent one), after projecting every sample to "now"
  with a drift slope fitted from the window halves (capped ±300 ppm).
  Structure borrowed from in-tree `SampleClock`/`ExtClockEstimator`:
  integer math, outlier gating, median filters.
- **TSF fast path:** when both peers are associated to the same BSS,
  `esp_wifi_get_tsf_time()` exposes the AP's beacon clock — a *shared*
  microsecond timebase. The offset collapses to a difference of
  `(tsf − local)` terms from `TSF_HINT`s: no RTT filtering, sub-millisecond
  by construction. Preferred over the measured path while fresh; Link,
  being portable C++ with no radio access, cannot do this. This is the
  path the shipping SoftAP/house-router rig actually sits on.

### 4.4 Transport

Start/stop intent is `(playing, toggle_session_us)`: the writer commits at
its own quantum boundary (the existing `TransportLatch` already quantizes
before `set_playing`), receivers flip when session time reaches the toggle
instant. With start/stop sync disabled the local transport is private, as
with Link 3.

### 4.5 The DAW bridge (VST3 plugin as a peer)

The plugin joins the mesh like any other node — discovery, announces,
ping/pong measurement, LWW state — but its node id carries `'D'` in the
high byte where firmware ids carry `'N'`, so a device always outranks it
and the laptop clock can never become the ghost reference (§4.2): the
session keeps a device's timebase and the plugin disciplines toward it.

On top of the node, `nsync::DawFollower` applies the authority rule *"the
DAW is authoritative while its transport runs"*: playing, the host's tempo
is level-asserted, a quantum boundary is re-anchored onto the host's bar
line whenever the phase error persists beyond tolerance, and the transport
is held running — mesh-side edits are corrected (rate-limited), because
VST3 gives a plugin no way to set host tempo, so the mesh cannot win that
argument without drifting off the DAW. Stopped, only edges write (a DAW
tempo change, the start/stop transition itself) and mesh edits stand. With
no live peers, or with drive switched off, the follower writes nothing —
which also lets the first device's announce win the seq-1 tie so the
plugin adopts the session's quantum and time domain rather than imposing
defaults. Mesh → DAW tempo remains the Max for Live device's job (§7).

## 5. Where it lives

- `components/neon_sync` — portable core (wire, peer clock, node).
  Dual-mode CMake like `neon_core`; no sockets, no ESP includes.
- `components/neon_sync_esp` — the socket/task/TSF glue implementing
  `hal::ILinkSession` (`nsyncesp::session()`).
- `nsync/daw_follower.hpp` + `nsync/playhead_mailbox.hpp` — the DAW
  bridge (§4.5): the sans-I/O policy that maps a VST3 host's
  `AudioPlayHead` onto node writes, and the wait-free seqlock the audio
  thread hands samples through. `plugin/client/src/sync.cpp`
  (`neon::client::SyncService`) is the desktop socket/thread glue — the
  third I/O wrapper around the same node, next to `neon_sync_esp` and the
  host simulator.
- `components/ableton_link` — with `CONFIG_NEON_SYNC`, `ablink::session()`
  forwards to Neon Sync and neither Link nor asio is compiled at all
  (`NEON_LINK_AUDIO` is unavailable; the no-op Link Audio facade stands
  in, and the editor reports streaming as not in this firmware).
- `host/fakes/fake_net.hpp` + `host/tests/test_nsync_*` — the simulator
  and CI suites (§6).
- CI: the `nsync` firmware matrix leg builds the Link-free image
  (`sdkconfig.ci.nsync`); the host job runs the protocol suites under
  ASan/UBSan.

Enable with `CONFIG_NEON_SYNC=y` (menuconfig: "NEON LINK — Neon Sync").
Link remains the default while Neon Sync is validated on hardware.

## 6. Host validation (what CI proves on every push)

`FakeNet` joins N in-process nodes over a fake segment with configurable
delay, jitter, loss, and partition, each node on its own offset + drifting
clock; everything randomized is seeded. The suites cover: wire roundtrip
and rejection, estimator selection/drift/TTL behavior, discovery, LWW
convergence under concurrent edits, transport propagation, BYE vs TTL
death, quantum propagation, island merge, the DAW bridge
(`test_nsync_follower.cpp`: authority while the host plays, edge-only
writes while stopped, stale-playhead release, grid alignment onto the
host's bars, the seqlock mailbox), and two soak scenarios:

- **Studio-grade** (1 ms jitter, 5% loss, ±20 ppm — ESP32 crystal spec):
  five peers hold worst-case pairwise phase error **< 500 µs** over 60
  simulated seconds (measured 240–340 µs across seeds).
- **Torture** (2 ms uniform jitter on every packet, 10% loss, ±50 ppm —
  beyond crystal spec): bounded degradation, worst pair **< 800 µs**.
- **TSF path** (shared BSS, deliberately ugly measured path): two peers
  hold **< 300 µs**.

The acceptance bar for hardware stays what it was for Link: the studio-mode
30-minute phase plot (docs/STUDIO_MODE_TEST_PLAN.md), indistinguishable or
better.

## 7. Remaining work

1. **C3 OLED Nearby spike** (`sdkconfig.defaults.linksync-c3oled-nsync`,
   `./scripts/flash_linksync-c3oled-nsync.sh`): first hardware bring-up
   of Neon Sync on unicore RISC-V. Isolated from the working Link image.
   SoftAP TSF + static Node are in `neon_sync_esp`. Studio-mode soak
   still outstanding.
2. **S3 bring-up + studio-mode A/B vs the Link build** on the real bench
   (docs/BENCH_NO_SCOPE.md methodology) — the numbers in §6 are simulator
   numbers until then.
3. **TSF through ESP-Hosted:** does the C6 give the P4 host usable TSF? If
   not, P4 targets ride the measured path (they already work there).
4. **Multicast reliability on the shipping rig:** loss rate of 1 Hz
   ANNOUNCE on SoftAP + house router decides whether a unicast fan-out
   fallback is needed (docs/STEM_SYNC.md chose unicast-with-repeats for
   launches for this reason). Related: with both SoftAP and STA up, lwIP
   routes the multicast out one netif; per-interface announce fan-out is a
   known v0 limitation to revisit at bring-up.
5. **Live bridges:** the VST3 `AudioPlayHead` → mesh direction ships in
   the plugin (§4.5: `nsync::DawFollower` behind
   `neon::client::SyncService`) — the plugin is a full Neon Sync peer and
   drives the mesh from any VST3 host's transport, host-validated like the
   rest of the protocol. Remaining: the Max for Live device for mesh →
   Live tempo, and bench time with a real device (the plugin side of the
   studio-mode A/B rides item 2). MIDI clock in/out works today with no
   new code.
6. **Licensing hygiene** regardless of protocol: the repo still has no
   LICENSE file, which blocks release with or without Link; and a GPL
   build variant (Link inside, source published, no locked secure boot)
   remains an option for users who need mixed Link sessions.
