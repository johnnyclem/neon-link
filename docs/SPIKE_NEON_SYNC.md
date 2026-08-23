# Research Spike — A Link-Free Sync Protocol ("Neon Sync")

**Status:** research spike. No code committed; this document is the deliverable.
**Question:** given the GPL obligations attached to Ableton Link, can we build
our own Link-style session protocol that serves Ableton Live and the whole
neon-link / link-sync device family first, and other DAWs later?
**Answer (short):** yes, and the codebase is unusually well positioned for it.
The entire product is already insulated from Link behind a 7-method interface
with three shipping Link-free implementations. What a replacement must supply
is discovery, leaderless tempo/beat-grid agreement, start/stop intent, and
cross-device clock-offset estimation. Ableton Live support does not require
speaking Link's wire protocol on day one — Live can be bridged through the
existing VST3 plugin and through MIDI clock paths the firmware already has.

---

## 1. Why this exists: the licensing problem, stated precisely

`README.md:203-208` and `docs/ARCHITECTURE.md:475-479` already flag it:
*"This must be resolved before any public release or sale."* The details are
worse than the summary:

1. **Link is dual-licensed GPLv2+ / commercial.** The open branch is GPLv2
   *or later*; the alternative is a proprietary license negotiated with
   Ableton (`link-devs@ableton.com`). LinkKit's permissive SDK license is
   iOS-only and does not help firmware.

2. **The "or later" is not optional for us — it collapses to GPLv3.**
   ESP-IDF and most of its components are Apache-2.0. Apache-2.0 is
   incompatible with GPLv2 but compatible with GPLv3. A firmware image that
   links Link against ESP-IDF can therefore only be distributed under
   **GPLv3 terms**, which brings §6 "Installation Information"
   (anti-tivoization): for a consumer device we must ship whatever is needed
   to install modified firmware. That forecloses secure boot / flash
   encryption with locked keys on any Link-carrying product, independent of
   whether we are happy to publish source.

3. **Copyleft scope is the whole image.** Everything statically linked into
   a Link-carrying firmware — including anything we might later want to keep
   closed — must be GPL-compatible. Today the repo is effectively
   all-rights-reserved with no LICENSE file anywhere, which is itself not a
   distributable state for a GPL-linked binary.

4. **What is *not* encumbered:** the wire protocol itself. Copyright
   protects Ableton's expression (their C++ source), not the protocol's
   functional behavior. Reimplementing a network protocol for
   interoperability is the Samba / Compaq-BIOS / Sega v. Accolade /
   Sony v. Connectix / Google v. Oracle lineage, and it is how
   `anweiss/ableton-link-rs` — a from-scratch native Rust implementation
   that interoperates with real Link peers — exists at all. (That project is
   itself GPLv3, so it is *evidence*, not a *dependency*.)

5. **Trademark is separate from copyright.** Even a perfect clean-room
   implementation cannot ship under the name "Ableton Link" or use the Link
   badge; those require Ableton's partner/branding agreement. Our own
   protocol under our own name has no such constraint.

So there are exactly four exits, and they are not mutually exclusive:

| Exit | Cost | What it buys | What it doesn't |
|---|---|---|---|
| **A. Buy the commercial Link license** | Unknown $ (quote from link-devs@ableton.com), per-product terms | Full Link interop, zero engineering | Doesn't remove the single-vendor dependency; terms unknown; likely recurring |
| **B. Ship GPL** | $0, but LICENSE work + GPLv3 installation-information obligations | Legitimacy for the current firmware | Forecloses locked secure boot; copyleft over the whole image; plugin/firmware licensing forever entangled |
| **C. Clean-room Link wire compatibility** | High (see §7) — protocol is officially undocumented, moving target | Native interop with Live *and* every Link-enabled app/DAW (Bitwig, Traktor, Serato, djay, FL Studio…) | Trademark still off-limits; version-drift risk when Ableton evolves the protocol |
| **D. Our own protocol + bridges** | Medium (see §7) | Full control, no copyleft, can exceed Link on our hardware (WiFi TSF, wired, ESP-NOW later), DAW-agnostic via plugin bridge | Live interop is bridged, not native, until/unless C is added |

**Spike recommendation: D now, A as a parallel de-risk (a quote costs an
email), C as a later optional module once D is proven.** Rationale in §8.

---

## 2. What Link actually does for us today (the replacement contract)

From `main/link_service.cpp` and the seam in
`components/neon_hal/include/hal/ILinkSession.hpp`, the firmware consumes
exactly six things, captured every 10 ms into `hal::LinkState`:

```
tempo_bpm, beat_at_origin, origin_us, quantum, playing, num_peers
```

and issues exactly seven commands:

```
start / capture / set_tempo / set_playing / request_beat_at_time /
set_start_stop_sync / set_quantum
```

Everything downstream — `neon::TimelineSnapshot` (Q32.32 µs-per-beat),
the seqlock bus, `ClockEngine` (24 PPQN MIDI clock + SPP), pulse engine,
UIs, web editor — is already Link-agnostic and host-tested. Three Link-free
implementations of the seam ship today:
`components/ableton_link/src/link_stub.cpp`, the Daisy
`InternalTimeline` (`daisy/src/internal_timeline.h`), and the CI stub leg.
`HANDOFF.md` §3.1 documents this as a deliberate design property.

So the replacement contract is precisely the four things Link supplies
*behind* that seam:

1. **Peer discovery** on the local network, zero-config, leaderless.
2. **Session/timeline agreement** — shared tempo and a shared beat grid
   (beat and phase modulo quantum), last-writer-wins, any peer may write.
3. **Start/stop intent** propagation (Link 3.x semantics: intent is shared,
   each peer applies it at its own quantum boundary).
4. **Clock-offset estimation** between peers' local monotonic clocks, good
   enough that phase error stays inside our own studio-mode acceptance
   numbers (`docs/STUDIO_MODE_RESULTS.md` methodology).

Nothing else. Link Audio is already dead in this codebase
(`docs/STEM_SYNC.md` supersedes it), and Stem Sync's launch messages are
protocol-agnostic.

---

## 3. How Link does it (for design reference, from public sources)

Assembled from Ableton's public conceptual docs, the LAC-2018 paper
(*Ableton Link — A technology to synchronize music software*, Goltz), the
community reverse-engineering notes (`westhom/AbletonLinkProtocol`), and the
existence proof of `ableton-link-rs`:

- **Transport:** UDP. Discovery and session state on multicast
  **224.76.78.75:20808** (last three octets spell "LNK"); joins/leaves via
  IGMP. Peers announce periodically and send a bye packet on exit; a peer
  that stops announcing times out.
- **Timelines, not transports:** every peer keeps its own timeline; the
  session agrees on a *relationship* — the (beat, time, tempo) triple plus a
  quantum. Beat 3 on one peer may map to beat 11 on another, but integer
  beats coincide and phase modulo quantum coincides.
- **Clock sync:** peers measure pairwise clock offset with unicast ping/pong
  exchanges (NTP-style: four timestamps per round trip), filter the samples
  (favoring minimum-RTT samples), and map session time onto each peer's
  local clock via a "ghost transform" (`GhostXForm`: an affine
  time-offset applied to the session timeline).
- **Agreement:** last-writer-wins on the timeline, ordered by session
  timestamps; the session identity travels with the state so late joiners
  adopt the current session rather than forking one.
- **Capture/commit API:** clients snapshot the state, modify, commit —
  exactly the shape `ILinkSession::capture()` + setters already mirror.

None of this is exotic. The hard part is not the algorithm, it is the
robustness engineering (peer churn, WiFi asymmetry, interface changes) — and
we already own a measurement harness for exactly that
(`tools/studio_mode`, `docs/BENCH_NO_SCOPE.md`).

**Clean-room hygiene note:** if we ever pursue exit C (wire compatibility),
implementation should work from independent documentation (packet captures,
the westhom notes, our own Wireshark dissection) — *not* from Link's GPL
source or from `ableton-link-rs` (GPLv3). For exit D (our own protocol) this
concern doesn't apply: we are not copying anyone's expression or wire
format. Several people on this project have read Link internals (the
Teensy/Daisy platform ports required it); for exit C we would want a
spec-writer/implementer split.

---

## 4. Proposed design: Neon Sync v0

Design goal: the smallest protocol that satisfies §2, expressed in the units
the codebase already uses (µs timestamps from `hal::IClockSource::now_us()`,
Q32.32 beat math from `neon/fixed_math.hpp`), beating Link on our own
hardware where our hardware allows it.

### 4.1 Transports (in order of preference at runtime)

1. **Infrastructure WiFi, UDP** — the default, per `docs/NEARBY.md` §4.
   Multicast group + port of our own (registered privately, e.g.
   `239.77.83.78:20809`), unicast for measurement. Works on every current
   target including P4-via-C6 (ESP-Hosted).
2. **Wired Ethernet (W5500)** — same packets; the existing lwIP route
   priority (`components/net_manager`) already prefers the cable.
3. **WiFi TSF fast path (same-AP optimization):** when two peers associate
   to the same BSS, `esp_wifi_get_tsf_time()` gives both a *shared*
   microsecond clock disciplined by AP beacons. Offset estimation collapses
   to exchanging `(tsf_us, local_us)` pairs — no RTT filtering needed, and
   accuracy is sub-millisecond by construction. Link cannot do this (it is
   portable C++ with no radio access). This is our chance to be *better*
   than Link on the SoftAP/house-router rig the linksync products actually
   ship into. Fall back to NTP-style measurement when TSF domains differ
   (different APs, wired, ESP-Hosted if TSF isn't forwarded — needs a spike
   task, §7).
4. **Explicitly out (for now):** BLE for sync (`docs/NEARBY.md`: star
   topology fights the leaderless model), ESP-NOW (no TSF without an AP,
   and no laptop can join; revisit for AP-less jam mode later).

### 4.2 Wire format

Fixed-layout little-endian packed structs (not CBOR/JSON — the consumers
are ISR-adjacent embedded code and a plugin; versioning via a header), all
messages ≤ one MTU:

```
Header:  magic "NSYN" | u8 proto_ver | u8 msg_type | u16 flags
         u64 node_id  | u64 session_id

ANNOUNCE (multicast, every 1 s, jittered ±200 ms; also on any state write):
         SessionState + u32 ttl_ms
BYE      (multicast, on clean shutdown): header only
PING     (unicast): u64 t1_local_us
PONG     (unicast): u64 t1_echo | u64 t2_remote_rx_us | u64 t3_remote_tx_us
TSF_HINT (unicast/multicast): u8 bssid[6] | u64 tsf_us | u64 local_us

SessionState:
         u64 state_seq          // Lamport-style version, see 4.4
         u64 origin_session_us  // session-time anchor
         i64 beat_at_origin_q32 // Q32.32, matches TimelineSnapshot
         u64 tempo_mpb_q32      // µs per beat, Q32.32 — native unit of
                                // neon/timeline.hpp, no doubles on the wire
         u32 quantum_mb         // milli-beats
         u8  playing            // + u8 start_stop_seq fields
         u64 writer_node_id
```

`SessionState` is deliberately isomorphic to `neon::TimelineSnapshot`
(`components/neon_core/include/neon/timeline.hpp:15-24`) so the ESP-side
session task is mostly a translation-free copy through the existing
`build_snapshot()` dedup (`neon/link_snapshot.cpp` already implements
material-change hysteresis: ~0.005 BPM relative tempo, 1e-4 beat phase).

### 4.3 Clock sync

Two estimators, selected per-peer:

- **TSF path:** same BSSID observed on both sides → offset =
  difference of `(tsf_us − local_us)` terms; a handful of samples,
  median-filtered. Expect ≤ 1 ms end-to-end phase error without trying.
- **Measured path:** PING/PONG rounds every 2 s per peer (burst of 4 at
  peer-join), keep a sliding window (~32) of `(offset, rtt)` samples,
  estimate offset from the minimum-RTT quartile (median of it), slew — never
  step — the applied offset, with drift tracked as a first-order rate term.
  This is exactly the structure of the in-tree `SampleClock`
  (`components/neon_core/src/audio/sample_clock.cpp`, self-described as
  "Ableton's HostTimeFilter minus the doubles") and `ExtClockEstimator`'s
  outlier rejection (median gate, EMA, hysteretic publish). **The clock
  discipline library for Neon Sync can be assembled from code we already
  test on the host.**

Acceptance target: reuse the studio-mode harness and hold Neon Sync to the
same numbers we measured for Link (`docs/STUDIO_MODE_RESULTS.md`), i.e. the
replacement is done when the 30-minute phase-error plot is indistinguishable
or better.

### 4.4 Agreement (leaderless, last-writer-wins)

Per `docs/NEARBY.md` §5.3 the product decision is already last-writer-wins
with no arbitration UI. Mechanism:

- `state_seq` is a Lamport counter: a writer sets
  `state_seq = max(seen) + 1`, ties broken by `writer_node_id`.
- Any peer receiving a higher `(state_seq, writer_node_id)` adopts the
  state and re-announces (gossip; convergence in O(1) multicast hops on one
  segment).
- `session_id`: highest `node_id` seen founds the session id; peers adopt
  the id they hear. Two islands merging adopt the state with the higher
  `state_seq` (musically: the most recent human action wins, which is the
  Link behavior users already expect).
- Start/stop is a separate `(start_stop_seq, playing, intent_session_us)`
  triple so that transport intent and tempo edits don't clobber each other;
  each peer applies intent at its own quantum boundary via the existing
  `TransportLatch` (`neon/transport.hpp`).
- Peer count = live ANNOUNCE senders within TTL; feeds `num_peers` (the
  e-paper/LCD UIs already render it).

### 4.5 Where it lives in the tree

New portable component `components/neon_sync/` (stdlib + sockets seam only,
host-testable like `neon_core`), plus a thin ESP task in
`components/neon_sync_esp/`. It implements `hal::ILinkSession` — the
fourth implementation of the seam — so **every target, UI, and test keeps
working unchanged**, and `CONFIG_NEON_SYNC` vs `CONFIG_NEON_LINK` becomes a
Kconfig choice during the transition. Simulation-first development: the
host build can run N in-process peers with a fake lossy clock-skewed
socket, which is how discovery/agreement gets tested in CI without radios.

---

## 5. Ableton Live support without Link

Live's native network sync is Link and nothing else, so "supports Live"
needs one of three bridges (all compatible with exit D; none GPL-encumbered):

### 5.1 VST3 bridge in the existing plugin (recommended, DAW-agnostic)

`plugin/` is already a JUCE 8 VST3 (AGPLv3, explicitly Link-free —
`plugin/README.md:45-49`) that lives in a Live set and talks HTTP to the
device. Add a Neon Sync peer to it:

- **Live/DAW → mesh (tempo master = DAW):** JUCE `AudioPlayHead` gives the
  plugin sample-accurate host tempo, PPQ position, bar phase, and transport
  state in every audio callback, in **every VST3/AU host** — Live, Logic,
  Cubase, Pro Tools (AAX later), Reaper, Bitwig. The plugin timestamps
  callback boundaries against the system clock (the same HostTimeFilter
  structure as `SampleClock`) and writes the mesh's `SessionState`. This
  path alone makes the system genuinely DAW-agnostic — including DAWs that
  never adopted Link.
- **Mesh → Live (tempo master = hardware):** VST3 cannot set host tempo.
  Options, in order of quality: (a) **Max for Live** companion device —
  the Live API (`live_set tempo`) can set tempo and start/stop the
  transport; M4L ships with Live Suite and the device is ours to license;
  (b) plugin emits MIDI clock into a virtual port Live follows (Live is a
  mediocre MIDI-clock follower; acceptable fallback); (c) UI-only: show
  "mesh wants 128.0" and let the human click. Start with (a) for Live and
  (c) generically.

### 5.2 MIDI clock (already shipping, zero new code)

The linksync dongle's whole job is clock out (24 PPQN + SPP +
Start/Stop/Continue, `neon::midi::ClockEngine`), and inbound external clock
into the session already exists (`ExtClockEstimator`,
`docs/COMPETITIVE_PARITY.md` row "External clock **into** the session").
Live-as-master → MIDI clock → dongle → mesh works **today** with a $30
USB-MIDI interface and no Link anywhere. This is the day-one Live answer
while 5.1 is built.

### 5.3 Clean-room Link wire module (later, optional)

Once Neon Sync is proven, a `components/link_compat/` speaking the Link
wire protocol (from independent docs + our own packet captures) would light
up Bitwig, Traktor, Serato, djay, FL Studio and Live natively, no plugin
needed. Treated as a separate spike with the clean-room process from §3 and
a protocol-drift monitoring cost. Not on the critical path.

---

## 6. What we lose vs. Link, honestly

- **Ecosystem interop on day one.** A Link session with third-party apps
  (Bitwig, iOS apps, Traktor) won't include Neon Sync peers until §5.3
  exists. Mitigation: the linksync dongle keeps a *GPL build variant*
  available (source published, no secure boot) for users who need mixed
  sessions, while the commercial line ships Neon Sync — dual-firmware, not
  dual-license.
- **Ten years of robustness hardening.** Link has seen every broken hotel
  router on earth. Budget real soak time on the studio-mode harness and on
  the a2 SDIO-jitter rig for the P4 targets.
- **The brand.** "Works with Ableton Link" is a marketing asset we can only
  claim via exit A or C.

What we gain, beyond the license exit: TSF-grade sync on the actual product
rig, a protocol that fits Q32.32 end-to-end (no doubles at the seam),
single-core silicon reach (`docs/SPIKE_ESP32C3_OLED.md` — Link+asio needs
dual-core; Neon Sync has no asio and no such constraint, reopening the
cheap C3 price floor), a place to put Stem Sync launch messages natively,
and freedom to add wired/ESP-NOW transports Link will never have.

---

## 7. Effort estimate and spike backlog

Rough sizing (one experienced firmware engineer, host-sim-first):

| Work item | Size |
|---|---|
| `neon_sync` core: wire structs, gossip agreement, peer table, host sim with lossy/skewed fake sockets + CI tests | ~2 weeks |
| Clock discipline: measured path (reusing `SampleClock`/`ExtClockEstimator` structure) + TSF path | ~2 weeks |
| ESP integration: `ILinkSession` impl, Kconfig choice, S3 + P4/C6 bring-up | ~1–2 weeks |
| Validation: studio-mode 30-min runs, A/B vs Link build, `BENCH_NO_SCOPE` phase checks | ~1 week |
| VST3 `AudioPlayHead` → mesh (DAW → hardware direction) | ~1 week |
| M4L device (mesh → Live tempo) | ~1 week |
| **Total to "Live supported, Link removed from commercial builds"** | **~7–9 weeks** |
| (Later, optional) `link_compat` clean-room wire module | ~4–6 weeks + ongoing drift watch |

Spike tasks to burn down first (each ≤ a day, each kills a risk):

1. **TSF through ESP-Hosted:** does the C6 expose usable TSF to the P4
   host? If not, P4 targets use the measured path only. (Bench: two Tab5s +
   one S3 on one AP, log `(tsf, local)` pairs.)
2. **Multicast reliability on the shipping rig:** loss rate of 1 Hz
   multicast ANNOUNCE on SoftAP + house router with phones present —
   decides whether ANNOUNCE needs unicast fan-out fallback (Link itself
   suffers here; `docs/STEM_SYNC.md` chose unicast-with-repeats for
   launches for this reason).
3. **Ping/pong offset floor on WiFi:** run the §4.3 estimator as a
   host+ESP pair, measure offset stability vs. the scope method in
   `docs/BENCH_NO_SCOPE.md`; target < ±500 µs steady-state.
4. **`AudioPlayHead` fidelity in Live 12:** confirm tempo-automation and
   loop-jump edge cases deliver usable `(ppq, sample_time)` pairs for the
   plugin bridge.
5. **Ask Ableton for a commercial quote** (exit A): zero engineering,
   bounds the build-vs-buy decision with a real number.

---

## 8. Recommendation

1. **Do exit D (Neon Sync) as the strategic path**, starting with the host
   simulator and the two clock-sync spikes. The `ILinkSession` seam means
   this is low-blast-radius: Link keeps working in-tree until Neon Sync
   passes the same studio-mode bar, then becomes a Kconfig choice, then a
   GPL-variant-only artifact.
2. **Send the licensing email now** (exit A). If Ableton's terms turn out
   to be cheap and acceptable, Neon Sync still pays for itself via TSF
   accuracy, C3-class silicon reach, and Stem Sync integration — but we'd
   ship v1 sooner. The quote is free information.
3. **Bridge Live via MIDI clock today, VST3 `AudioPlayHead` + M4L next**
   (§5.1–5.2). Defer wire-compatible Link (§5.3) until Neon Sync is proven
   and worth the maintenance tail.
4. **Fix the repo's own licensing hygiene regardless:** add a LICENSE file
   and per-component SPDX headers, and keep the AGPLv3 plugin / firmware
   boundary documented — whichever exit wins, the current no-license state
   blocks release.
