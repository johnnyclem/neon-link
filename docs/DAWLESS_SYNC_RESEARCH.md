# Research Spike — DAWless sync & discovery ("Bonjour for electronic music gear")

**Working name:** neon-sync
**Status:** research spike — no code changes proposed yet
**Time box:** this document (analysis) + a follow-up 3-day bench spike (§7) before any protocol work is committed
**Date:** 2026-08-22

---

## 0. The question this spike answers

Ableton Link is dual-licensed: **GPLv2+ or a commercial license negotiated with
Ableton** (`link-devs@ableton.com`). Our firmware links Link, so every shipping
build is either GPL-encumbered or blocked on a commercial agreement —
`README.md` §Licensing Notes and `docs/ARCHITECTURE.md` §Licensing already
flag this as a must-resolve before sale. The free LinkKit SDK does not help:
its royalty-free license is **explicitly iOS-only**, revocable on 14 days'
notice, and forbids redistribution of the SDK.

So: **should we build our own Link-type system** — totally DAWless, acting as
the "Bonjour for electronic music gear," supporting all neon-link/link-sync
devices out of the box, and optionally DAW-agnostic via a VST bridge — **or
adopt an existing open standard (e.g. BLE MIDI)?**

Short answer, argued below:

1. The "Bonjour" half of the idea is **already an open, free standard** —
   mDNS/DNS-SD — and we already ship an mDNS responder. Claiming a service
   type and TXT schema for music gear costs us nothing in licensing and is
   worth doing regardless of any other decision.
2. The **session half** (leaderless shared tempo / beat / phase / quantum /
   start-stop) has **no open-standard equivalent**. BLE MIDI, RTP-MIDI, and
   even the new Network MIDI 2.0 (UDP) standard are *transports for MIDI
   messages*, not tempo-consensus protocols. If we want Link semantics without
   Link's license, we have to build or clean-room them.
3. The hard part is not the wire format; it is the tuned clock-sync filtering
   and the ten years of ecosystem (hundreds of Link-enabled apps). A
   neon-sync protocol is viable gear-to-gear, but it does not talk to Live —
   that still requires Link (GPL/commercial), a clean-room Link
   implementation, or a bridge (VST/M4L) with real limitations (§4.6).

---

## 1. What Link actually is (decomposed)

To replace it, separate what Link bundles into four layers:

| Layer | What Link does | Open-standard equivalent? |
|---|---|---|
| **Discovery** | Proprietary UDP multicast beacons on `224.76.78.75:20808` (the group spells "LNK"); per-interface join, peer gossip | **Yes** — mDNS/DNS-SD is the standard way (Network MIDI 2.0 uses it; we already advertise `_http._tcp`) |
| **Clock sync** | Pairwise unicast ping/pong measurements against a session reference clock ("ghost" transform), min-RTT style filtering; `HostTimeFilter` linear regression to map system time onto the audio callback | **Partially** — NTP/PTP exist, but nothing music-shaped and consumer-network-tolerant; this is Link's crown jewel |
| **Session state** | A shared timeline `(tempo, beat-at-origin, origin-time)` + quantum-based phase alignment; **leaderless** — any peer may change tempo, last-writer-wins, peers join/leave freely | **No** — nothing open does tempo consensus |
| **Transport control** | Optional start/stop sync, quantized launch | **No** (MIDI Start/Stop exists but is master/slave, not consensus) |

The protocol is undocumented and unversioned; the reference implementation
*is* the spec (the LAC 2018 paper by Florian Goltz describes the design; a
partial reverse-engineering effort exists at `westhom/AbletonLinkProtocol`).
Our tree already contains three asio-free reimplementations of Link's
*platform* layer (`teensy41/link_platform/`, `daisy/netlink/link_platform/`,
`components/ableton_link/link_overrides/`) — so we know exactly how much of
Link is portable plumbing versus sync logic.

Everything downstream of Link in our firmware is already protocol-agnostic:
`hal::ILinkSession` (`components/neon_hal/include/hal/ILinkSession.hpp`) is
the seam, `CONFIG_NEON_LINK_STUB` proves the whole product builds and runs
without Link, and `app_state/timeline_bus.h` is where any sync source injects
the timeline. **A replacement protocol is a new implementation of
`ILinkSession`, nothing more invasive.**

---

## 2. The licensing problem, precisely

- **Link library:** GPLv2+ *or* commercial (terms not public; case-by-case
  with Ableton). GPLv2 obligations attach to the whole firmware image we
  distribute, since Link is statically linked into it.
- **LinkKit:** royalty-free but **iOS applications only**, non-redistributable,
  terminable — unusable for firmware, desktop, or embedded.
- **Our current posture:** `docs/LINKSYNC.md` declares the link-sync target
  "same repo, same GPLv2+ license." That is a *coherent* open-hardware
  strategy — GPL firmware is not automatically a business problem — but it
  forecloses proprietary firmware and imposes source-offer obligations on
  every unit sold.
- **Existing firewall precedent in-tree:** the VST3 (`plugin/`) is AGPLv3
  (JUCE 8's open option) and deliberately does **not** link Link — it speaks
  HTTP/REST to the module and resolves it via mDNS. This shows we already
  know how to keep Link's license contained to the firmware image.
- **Clean-room note:** wire protocols and interoperability facts are not
  copyrightable; a from-scratch implementation of Link's protocol (from the
  paper + packet captures, by engineers who have not read Link's GPL source)
  would not inherit the GPL. The practical risks are different: the protocol
  is unversioned and can change under us, "Ableton Link" is a trademark we
  could not use to market compatibility, and it likely burns the relationship
  that a cheap commercial license would preserve. Also note our tree already
  contains modified copies of Ableton GPL headers
  (`components/ableton_link/link_overrides/…`), so the clean-room team must
  be disjoint from anyone who has worked in those files.

---

## 3. Option space

### A. Status quo — GPL firmware with Link

**Pros:** zero engineering; full Link ecosystem (Live, hundreds of apps,
Circuit Happy interop); GPL is compatible with selling hardware.
**Cons:** whole-firmware GPL obligations forever; no proprietary features
later without a rewrite; per-unit source-offer logistics; Link's asio/C++
footprint is a real burden on small targets (we maintain three platform
ports already).

### B. Commercial Link license from Ableton

**Pros:** cheapest engineering path to a proprietary product; keeps the
ecosystem; Ableton has granted these to small hardware makers (Circuit Happy
ML:2m class of product exists).
**Cons:** terms are opaque and negotiated; a dependency on Ableton's
goodwill for the core feature of the product line; does nothing for the
"open protocol for gear" ambition.

### C. Clean-room reimplementation of the Link wire protocol

**Pros:** Link-ecosystem interop without GPL and without Ableton's
permission; we control the code (could be MIT — genuinely useful to the
whole hardware community, which is a marketing story of its own).
**Cons:** the sync math is the hard 20%; protocol can change without notice
(every Live update is a regression risk); cannot use the Link name/badge;
requires provable clean-room hygiene (see §2); ongoing conformance testing
against a black box.

### D. Our own protocol — "neon-sync" (the ask)

DNS-SD discovery (`_neonsync._udp` + TXT metadata) + an NTP-style pairwise
clock filter + a Link-shaped session document (tempo, beat origin, quantum,
playing), CRDT-ish last-writer-wins so it stays leaderless.

**Pros:**
- **License-clean** (MIT/Apache-2.0) — solves the original problem completely
  for gear-to-gear sync; firmware can be proprietary or open at our choice.
- **Designed for hardware first:** integer timeline (we already have the
  Q32.32 `TimelineSnapshot`), tiny UDP frames, no asio/C++ exception
  requirements — trivially portable to every board in `main/Kconfig.projbuild`,
  the adapters, RP2040, even the C3.
- **Discovery is richer than Link's:** DNS-SD TXT records can carry device
  identity, capabilities (CV outs, MIDI ports, stems — see `docs/STEM_SYNC.md`),
  firmware version, battery — the actual "Bonjour for electronic music gear"
  story. Link's discovery carries none of that.
- Room for features Link refuses to have: named sessions, scene/section
  broadcast (`tools/s1_scene_launch` groundwork), stem transfer negotiation,
  Wi-Fi-less BLE fallback for *provisioning into* a session.
- If published as an open spec with a permissive reference implementation,
  it is a moat-by-openness play: other manufacturers can adopt it, and every
  adopter makes our hardware more useful.

**Cons:**
- **Ecosystem cold start is the killer risk.** Day one it syncs
  neon-link ⇄ link-sync ⇄ adapters — i.e. only our own devices, which
  already sync via Link today. Nobody else speaks it (xkcd 927: "now there
  are 15 competing standards").
- **No Live interop by itself.** The flagship use case ("sync my modular to
  Live") still needs Link in the image (A/B), a clean-room stack (C), or a
  DAW bridge (§4.6) with real limitations.
- The clock-filter engineering is genuinely hard to get as good as Link on
  congested consumer Wi-Fi; Ableton spent years tuning this. Our own
  measurements (`docs/STUDIO_MODE_RESULTS.md`, `tools/studio_mode/`) show how
  sensitive phase error is to RF conditions.
- We become a standards body: spec versioning, conformance tests,
  third-party support burden.

### E. Adopt an existing open standard

Analyzed one by one in §4; none provides the session layer, but one
(Network MIDI 2.0) is a strong fit for discovery + transport.

---

## 4. Comparison to existing open standards

### 4.1 BLE MIDI (the baseline asked about)

What it is: MIDI 1.0 byte stream over a BLE GATT characteristic, 13-bit
millisecond timestamps, standardized by the MIDI Association; we already ship
it as a peripheral (`components/ble_midi/`).

| | Assessment |
|---|---|
| License | Open/free ✅ |
| Discovery | BLE advertising — works, but pairwise and OS-mediated |
| Topology | **Star, central/peripheral** — a phone or one hub connects to each device; peers don't mesh. `docs/NEARBY.md` §4 already rejected BLE as a sync transport for exactly this reason |
| Timing | Best case ~3 ms between two dedicated BLE5 devices; realistic 10–15 ms round-trip with jitter, because central OSes clamp connection intervals (7.5–15 ms) and widely ignore or round the BLE-MIDI timestamps |
| Tempo model | MIDI clock (24 PPQN) + Start/Stop only — master/slave, no phase/quantum, no tempo consensus, no late-join phase alignment |
| Verdict | **Keep for what it's good at** (notes, provisioning, phone control) — it is not a Link alternative. A drum machine following 24 PPQN over a transport with ±5 ms jitter audibly flams against a tight wired clock. |

### 4.2 Network MIDI 2.0 (UDP) — the new one to watch

Ratified by the MIDI Association + AMEI in **November 2024** (spec
M2-124-UM): UMP packets over UDP, session protocol with retransmission,
and — directly relevant — **discovery via mDNS/DNS-SD** (`_midi2._udp`).
Windows MIDI Services is shipping it; a Zephyr host stack is in review;
early iOS apps exist.

**Pros:** genuinely open and free; *is* "Bonjour for MIDI gear" at the
discovery layer; UMP JR-timestamps give sub-ms message timing; Ethernet and
Wi-Fi; industry momentum (this is likely what the next decade of hardware
speaks).
**Cons:** transports MIDI, doesn't do tempo consensus — clock is still
sender-driven 24 PPQN semantics; no beat/phase/quantum session; embedded
implementations are brand new (we'd be early adopters); no macOS/iOS native
stack announced yet.
**Verdict:** the right *transport + discovery* standard to align with; not a
session protocol. neon-sync could be layered as a UMP-adjacent service using
the same DNS-SD conventions, so one mDNS browse finds both.

### 4.3 RTP-MIDI / AppleMIDI (RFC 6295)

**Pros:** mature; built into macOS/iOS and rtpmidid on Linux; Bonjour
discovery (`_apple-midi._udp`); good recovery journal; lots of hardware
(iConnectivity, BomeBox).
**Cons:** MIDI 1.0 semantics; session initiator/participant model; clock sync
exists but serves the journal, not musical phase; Windows needs third-party
drivers; again no tempo consensus. Superseded in momentum by 4.2.

### 4.4 Wired MIDI clock (TRS/DIN) — already shipped

Tightest jitter of anything here (our GPTimer/RMT outputs), zero licensing,
but point-to-point, master/slave, no discovery, no late-join phase. It is the
*output* of our products, not the mesh between them.

### 4.5 OSC

A message format, not a protocol: no discovery, no clock, no session — every
deployment is bespoke (our `tools/s1_scene_launch` AbletonOSC daemon is
exactly such a bespoke integration). Useful as a control-surface sidecar,
irrelevant as a sync standard.

### 4.6 "Any DAW via a VST" — reality check on the bridge idea

The AGPL VST3 precedent in `plugin/` extends naturally: a neon-sync VST
could join the mesh from inside any DAW. But hosts constrain it:

- **Follower direction works well:** the plugin reads the host playhead
  (tempo, PPQ position, transport) every block and can publish the DAW's
  timeline into the mesh with sample accuracy.
- **Leader direction is host-limited:** most DAWs (including Live) do not
  let a plugin *set* host tempo or start playback. VST3 tempo control is at
  best a host-specific extension; in Live it needs a Max for Live device, in
  others a virtual-MIDI-clock loopback. So "anyone twists a knob on the
  modular and the DAW follows" degrades to per-DAW hacks — Link is the only
  thing Live itself follows.
- Conclusion: a VST makes neon-sync *DAW-visible*, not fully *DAW-symmetric*.
  Fine for "DAW is the tempo boss" workflows; not a substitute for Link's
  bidirectionality with Live.

---

## 5. Decision matrix

| | License | Discovery | Leaderless tempo/phase | Jitter (Wi-Fi) | Works with Live today | Embedded cost | Ecosystem |
|---|---|---|---|---|---|---|---|
| Link (GPL) | GPLv2+ ⚠️ | proprietary multicast | ✅ best-in-class | tuned filters ✅ | ✅ native | high (asio, C++ exceptions) | huge |
| Link (commercial) | $/negotiated ⚠️ | 〃 | ✅ | ✅ | ✅ | 〃 | huge |
| Clean-room Link | ours (MIT) ✅ | 〃 (must match) | ✅ if we nail it | must re-derive | ✅ until protocol drifts ⚠️ | our choice | huge, borrowed |
| **neon-sync (D)** | ours (MIT) ✅ | DNS-SD ✅ richer | ✅ by design | must engineer ⚠️ | ❌ (bridge only, §4.6) | tiny by design ✅ | zero, day one ❌ |
| Network MIDI 2.0 | open ✅ | DNS-SD ✅ | ❌ | JR timestamps good | ❌ (not yet) | small, new | growing |
| RTP-MIDI | open ✅ | Bonjour ✅ | ❌ | fair | partial (as MIDI device) | small | mature/legacy |
| BLE MIDI | open ✅ | BLE adv | ❌ | poor (5–15 ms) ❌ | partial (as MIDI device) | shipped ✅ | large |
| Wired MIDI clock | open ✅ | none | ❌ | best ✅ | via interface | shipped ✅ | universal |

---

## 6. Recommendation — layered, not either/or

1. **Claim the discovery layer now, on the open standard.** Extend our
   existing mDNS advertisement (`components/net_manager/src/net_manager.cpp`)
   with a `_neonsync._udp` service + TXT capability schema (and track
   `_midi2._udp` conventions from Network MIDI 2.0 so one browse finds
   everything). This is the "Bonjour for electronic music gear" deliverable,
   it is license-clean, it improves the VST's device pairing immediately
   (replacing the fragile `.local` `getaddrinfo` path noted in
   `plugin/client/src/bind.cpp`), and it commits us to nothing on sync.
2. **Keep Link behind `ILinkSession` for ecosystem compatibility** and open
   the commercial-license conversation with Ableton in parallel — the answer
   changes the economics of everything else, and option B may be cheap.
3. **Spike neon-sync as a second `ILinkSession` implementation** (stub seam
   already proves this is contained), gear-to-gear only, with the bench
   methodology we already built (`tools/studio_mode/` phase-error tooling).
   Decide after measurement, not before: if we cannot beat ±1 ms phase error
   device-to-device on hostile Wi-Fi, the custom protocol is not worth its
   maintenance and we fall back to A/B + layer-1 discovery.
4. **Do not clean-room the Link wire protocol (C)** unless the Ableton
   conversation fails *and* neon-sync measurement succeeds — it is the
   highest-risk path and only makes sense as leverage.
5. **Ship the VST bridge as a follower-first feature** and market it
   honestly (DAW-visible, not DAW-symmetric).

## 7. Follow-up bench spike (time box: 3 days)

House convention: `tools/ns1_neon_sync/` with `README.md`, `RUNBOOK.md`,
committed `results_*.json`.

- **Q1** — Can a naive NTP-style filter (min-RTT over sliding window +
  median/EMA, reusing `neon/ext_clock.hpp` estimator patterns) hold two
  ESP32-S3s within ±1 ms phase error over 30 min on the RF-hostile profile
  from `tools/studio_mode/rf_scan_logger.py`? Measure with two scoped CLK
  outputs, same rig as `docs/STUDIO_MODE_TEST_PLAN.md`.
- **Q2** — Does DNS-SD browse+resolve complete in <2 s on ESP-IDF mdns
  1.11.3 in *browser* role (we have only ever advertised)? Memory cost?
- **Q3** — Tempo-change convergence: with 3 peers and last-writer-wins,
  does a tempo nudge propagate and settle (<1 beat at 120 BPM) without
  oscillation?
- **Q4** — Frame format draft ≤ 48 bytes/state packet, integer-only
  (Q32.32 beats, µs origin), versioned header.

**Done means:** (1) Q1–Q3 answered with committed measurements, (2) a
one-page protocol sketch reviewed against `ILinkSession`, (3) a go/no-go
recommendation appended to this document, (4) no changes to shipping
firmware defaults.

## 8. Risks

| Risk | Mitigation |
|---|---|
| xkcd-927 / zero adoption | Ship it as *our* mesh first; publish spec MIT only if it measures well; align discovery with Network MIDI 2.0 conventions |
| Clock filter never matches Link | Gate on Q1 before writing any protocol code |
| Split-brain UX (Link session *and* neon-sync session with different tempi) | One active session source per device, explicit in UI; bridge device rebroadcasts, never merges |
| GPL contamination of clean-room work | Not doing C now; if ever, disjoint team from `link_overrides/` authors |
| Ableton relationship | Pursue B conversation before shipping anything that could read as adversarial |

---

## Appendix — status against the tree (2026-08-22)

**Already shipped (the spike stands on this):**

| Needed for neon-sync | Where it already exists |
|---|---|
| Protocol-agnostic session seam | `components/neon_hal/include/hal/ILinkSession.hpp`, `CONFIG_NEON_LINK_STUB` (`components/ableton_link/Kconfig`), CI stub leg |
| Timeline injection point | `components/app_state/include/app_state/timeline_bus.h` |
| Integer timeline + snapshot dedup | `components/neon_core/include/neon/{timeline,link_snapshot}.hpp` |
| Tempo-estimation filter prior art | `components/neon_core/include/neon/ext_clock.hpp` + `host/tests/test_ext_clock.cpp` |
| mDNS advertise + TXT records | `components/net_manager/src/net_manager.cpp` (`_http._tcp`, `fw`/`name`/`id` TXT) |
| Multi-interface policy | `components/neon_core/include/neon/net/preference.hpp`, `link_overrides/.../ScanIpIfAddrs.hpp` |
| Phase-error bench methodology | `tools/studio_mode/` (+ `docs/STUDIO_MODE_TEST_PLAN.md` / `_RESULTS.md`) |
| Asio-free UDP platform layers | `teensy41/link_platform/`, `daisy/netlink/link_platform/` |
| License firewall precedent | `plugin/` (AGPLv3, REST-not-Link), `plugin/README.md` |

**Partial:**

| Item | Gap |
|---|---|
| mDNS | Advertise-only today; browser role (device-side discovery) never exercised — Q2 |
| BLE | Peripheral-only; no central/scanning — irrelevant for sync (§4.1) but limits BLE-assisted onboarding ideas |
| Telemetry | `neon/telemetry/linksync_csv.hpp` reports Link peers; would need a source-agnostic peer field |

**Conflict:**

| Item | Tension |
|---|---|
| `docs/LINKSYNC.md` "same GPLv2+ license" | A permissively-licensed neon-sync inside a GPL firmware image is fine, but the *incentive* (proprietary firmware) requires removing Link from that image — the two goals meet only in a Link-free build variant |
| `docs/NEARBY.md` §4 BLE rejection | Reaffirmed here; any BLE-MIDI-as-sync suggestion contradicts prior decision and the measurements in §4.1 |

## Sources

- [Ableton Link repository (dual license statement)](https://github.com/ableton/link) · [LICENSE.md](https://github.com/Ableton/link/blob/master/LICENSE.md) · [GPLv2 text](https://github.com/Ableton/link/blob/master/GNU-GPL-v2.0.md)
- [LinkKit docs](https://ableton.github.io/linkkit/) · [LinkKit SDK license (iOS-only)](https://github.com/Ableton/LinkKit/blob/master/LICENSE.md)
- [Goltz, "Ableton Link — A technology to synchronize music software," LAC 2018](https://lac.linuxaudio.org/2018/pdf/42-paper.pdf)
- [westhom/AbletonLinkProtocol (reverse-engineering notes)](https://github.com/westhom/AbletonLinkProtocol)
- [Network MIDI 2.0 (UDP) overview — MIDI.org](https://midi.org/network-midi-2-0-udp-overview) · [M2-124-UM spec PDF](https://amei-music.github.io/midi2.0-docs/amei-pdf/M2-124-UM_v1-0_Network-MIDI-2-0-UDP.pdf) · [Zephyr host stack PR](https://github.com/zephyrproject-rtos/zephyr/pull/93933)
- [CME, "The Truth About Bluetooth MIDI" (BLE latency/jitter)](https://www.cme-pro.com/the-truth-about-bluetooth-midi/) · [Nordic DevZone, "Optimizing BLE-MIDI with regards to timing"](https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/optimizing-ble-midi-with-regards-to-timing-1293631358)
- [CDM, "Ableton is opening Link to everyone"](https://cdm.link/ableton-opening-link-everyone-starting-today/)
- Link on ESP32 prior art: [CircuitHappy/link_esp32_example](https://github.com/CircuitHappy/link_esp32_example) · [esp-idf-ableton-link](https://github.com/docwilco/esp-idf-ableton-link)
