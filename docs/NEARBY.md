# PRD + SPEC — Nearby

**Working name:** Nearby
**Status:** proposal. Mostly a defaults and UX change, not new architecture.

---

## 1. The moment this came from

Two prototypes — a XIAO ESP32S3 and an e-paper board — plugged into power.
No computer. Not connected to anything. WiFi already configured from an
earlier session.

**They found each other, joined a Link session, and started working.**

Nobody built that. It is what Link does when you stop assuming a computer is
in the room.

---

## 2. The insight

**Link is already leaderless.** No master, no host, no designated clock
source. Any peer can change tempo and everyone converges. Peers join and
leave without ceremony.

So Nearby is not a mode to build. It is a mode to **stop hiding**.

The work is removing the assumption that a session begins with Ableton Live,
and making the zero-computer path the default rather than a fallback.

**This is not a mode. It is the default. Ableton is the special case.**

---

## 3. The user and the job

**Primary:** musicians with hardware — synths, drum machines, pedals, grooveboxes
— who want to play together in time. No laptop. Possibly no interest in one.

**The pain, named the way they would name it:**

- MIDI cables across the room
- Which box is the master, and what happens when they want to swap
- "Out" vs "Thru" and the gear that only has one
- TRS-A vs TRS-B vs 5-pin — three standards, wrong adapter, no error message
- BLE MIDI adapters, one per device, each with its own pairing ritual

**The job:** plug in, play, in time, without thinking about any of that.

---

## 4. Transport — use the router

**Default: ordinary infrastructure WiFi.**

That is what already worked. Every rehearsal room, house, and most venues
have a network. Best range, proven path, no new code.

### Why not BLE

BLE is a **star topology** — one central, N peripherals. That fights Link's
peer model directly. You would be reimplementing tempo sync over GATT instead
of using Link at all, and inheriting a designated-master problem Link does
not have.

BLE's legitimate role is **provisioning** (§6), not sync.

### Why SoftAP is the fallback, not the default

Measured and working — multicast traverses, sessions hold 30 minutes. But it
needs a **designated host**, which breaks the leaderless property. Someone's
box is the AP; if that person packs up, the session dies.

Ship it as "no network here" mode, and be explicit that it is a different
topology with a single point of failure.

### Decision table

| Situation | Transport |
|---|---|
| Known network available | **infrastructure — default** |
| No network / unknown venue | SoftAP, one device hosts |
| Never | BLE for sync |

---

## 5. Behaviour

### 5.1 Boot

1. Power on
2. Rejoin last known network (no user action)
3. Start Link
4. Display tempo, peer count, transport

**Under 15 seconds, zero interaction.** That is the whole product.

### 5.2 First run

Only once, and only if there is no saved network:

1. Device shows it needs setup — legibly, in large type (§7)
2. BLE provisioning via phone, or SoftAP fallback
3. From then on, never again

### 5.3 Tempo

Any peer can set it; everyone follows. On a device with an encoder, turning it
sets the session tempo. No master mode, no arbitration UI — Link handles it.

**Corollary:** if two people change tempo at once, last writer wins. That is
Link's behaviour, it is fine, and it should not be papered over with a lock.

### 5.4 Alone

A device with no peers still runs its own clock and emits MIDI. It is a
tempo source on its own. Peers joining later converge to it.

---

## 6. Setting the things that must be set

Zero-config cannot mean zero-configuration-ever. Two things genuinely need a
value.

### 6.1 MIDI cable type

A switched jack detects **that** a cable is inserted, not **which type**. So
detection is not possible.

**Make it a one-time setting, per device, during provisioning.** Phone shows
three pictures — TRS-A, TRS-B, 5-pin — user taps one. Stored forever.

**Do not ship three cables and hope.** The honest promise is "set it once,
forget forever," not "no dongles."

### 6.2 Out vs Thru

The dongle has TX only. No chaining.

**Say this plainly rather than treating it as a gap:** one per player, no
chaining, that is the design. Chaining is the problem this replaces.

---

## 7. Display

### 7.1 When connected

Tempo, peer count, transport state. Downbeat indicator where the hardware
allows.

### 7.2 When not connected — the screen is an instruction card

The display's unique value is being readable **when nothing else is working**.
Anything the web UI can show, let the web UI show.

Not connected means the screen's entire job is the next action:

```
JOIN WI-FI

NEON-LINK-6BA0
pass: 4f2a9c

then open
neon-link.local
```

- Network name largest — it is what they match against a phone list
- No truncation. If it does not fit, shrink the font, never clip
- Password only while unassociated; hide once a station joins
- **Distinguish "nobody joined" from "someone tried and failed"** — different
  instructions. The failed case is where this earns its keep: *"wrong
  password — use 4f2a9c"* is the error macOS refuses to give
- In STA mode and unreachable: show `neon-link.local` **and** the IP, since
  mDNS is exactly what fails silently

### 7.3 E-paper note

**Do not put the downbeat indicator on e-paper.** Refresh is far too slow.
Tempo, peer count and connection state are ideal — they change rarely and
cost nothing to hold. The beat flash stays on an LED.

E-paper's real win here is a pedalboard display that is readable in daylight
and consumes nothing while static.

---

## 8. Scope

### In

- Rejoin last network and start Link on boot, no interaction
- BLE provisioning, SoftAP fallback
- Cable type as a one-time provisioning choice
- Instruction-card display for every disconnected state
- Tempo settable from any peer
- Standalone operation with zero peers

### Out

- BLE as a sync transport
- MIDI thru / merge — different product
- Any master/slave UI — Link is leaderless, do not invent a hierarchy
- Automatic cable-type detection — not physically possible

### Done means

1. Two devices, cold power-on, no computer → Link session inside 15 s
2. Third device joins mid-session and converges
3. Tempo changed on any device → all follow within a beat
4. Router rebooted → all reconnect without power cycling
5. A musician who has never seen the product gets a synth clocked from it
   without the manual

Item 5 is the actual bar. Items 1–4 are how you get there.

---

## 9. Positioning

The demo is the product: **plug in two boxes, they find each other, play.**

Not "wireless MIDI sync." That is a feature list. It is *"stop running cables
and arguing about which box is the master."*

The Ableton integration is a mode that appears when a computer is present.
Most sessions will not have one.
---

## Appendix — status against the tree (2026-08-20)

Added when this PRD landed in the repo. Everything above is the proposal
as written; this maps it onto what the firmware already does.

### Already shipped

| PRD item | Where |
|---|---|
| §5.1 boot → rejoin → Link, zero interaction | `main/wifi.cpp` walks up to four stored networks with per-network retries, `WIFI_PS_NONE`, retries forever — router-reboot recovery included. [LINKSYNC.md](LINKSYNC.md) DoD already requires cold boot to Link peer under 15 s |
| §5.2 first-run BLE provisioning, SoftAP fallback | `main/provision.cpp` — BLE (security 1, PoP from MAC), SoftAP raised after 90 s. Skipped entirely once credentials exist |
| §5.3 tempo from any peer, no arbitration | Link itself; encoder tempo on panel targets goes through the same timeline bus |
| §5.4 standalone clock with zero peers | Link runs its own timeline; `ClockEngine` emits MIDI regardless of peer count |
| §4 SoftAP as explicit fallback, not default | AP policy (fallback / always / off) is already a stored setting |
| §7.3 no downbeat on e-paper | `main/epd_service.cpp` fingerprints only tempo / peers / connection state — beat and phase never reach the glass. Beat flash is the LED |

### Partial — the real work in this PRD

**§7.2 instruction card.** The EPD/LCD targets show the AP SSID and
password when unprovisioned, but against the spec:

- They show `http://192.168.4.1`, not the mDNS name; in STA mode there is
  no on-glass fallback showing `.local` **and** IP when unreachable.
- The AP password stays on screen after a station joins.
  `net_manager.cpp` sees `AP_STACONNECTED` / `AP_STADISCONNECTED` but
  only logs them — no station count is surfaced to the display code.
- No distinction between "nobody joined" and "someone tried and failed".
  Implementable: a station with the wrong AP password shows up as
  STACONNECTED followed quickly by STADISCONNECTED with a handshake
  reason — track that pair and change the card's copy.

**Defaults and positioning.** Docs and first-run copy still frame
Ableton as the session anchor. Flipping that (§2, §9) is copy and docs
work, not firmware.

### Conflict — §6.1 cable type

Current dongle hardware hardwires TRS-A: D0 → 220 Ω → tip, 3V3 → 220 Ω
→ ring ([LINKSYNC.md](LINKSYNC.md) calls A/B "a passive adapter, not a
firmware mode"). A provisioning-time A/B choice needs the carrier to
route both tip and ring through GPIOs. Either the v1.1 carrier adds
that, or on existing hardware the promise stays "TRS-A default plus
passive adapters" and the provisioning picker appears only on boards
that can honor it.
