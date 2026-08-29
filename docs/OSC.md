# OSC control

UDP OSC 1.0, the native language of TouchOSC, show controllers, and
lighting rigs. **Off by default** — this is an unauthenticated LAN
surface, the same trust posture as the web editor's origin gate: turn
it on only on a network that is yours. Enable it on the web editor's
System page (or `PUT /api/config` with `{"osc":{"enabled":true}}`).

Ported from the SolarOS evaluation
([SOLAROS_PORTS_HANDOFF.md](SOLAROS_PORTS_HANDOFF.md) §4); the codec
and the outbound filter policy are portable, host-tested code
(`neon/osc/codec.hpp`, `neon/osc/bindings.hpp`).

## Inbound (listen port, default 9000)

Every message lands on the same `ControlCommand` funnel as the web
editor and the panels, with the same clamps. One argument per message
(`f`, `i`, `T`, or `F`); bundles are accepted with the immediate
timetag only.

| Address | Argument | Action |
|---|---|---|
| `/neon/tempo` | float BPM | set tempo (clamped 20–999) |
| `/neon/nudge` | int ±1..±50 | nudge tempo by whole BPM |
| `/neon/transport` | 1/T = play, 0/F = stop | quantized transport |
| `/neon/toggle` | any | toggle transport |
| `/neon/tap` | any | tap tempo |
| `/neon/resync` | 0 = next loop, 1 = now | resync |

Unknown addresses and out-of-range values are counted and ignored —
they never error the packet. Inbound is rate-limited to 100 packets/s.

TouchOSC example: a fader sending `/neon/tempo` 60–200 and a button
sending `/neon/toggle` is a complete remote.

## Outbound (optional)

Set **Send target** to `host:port` (IPv4 literal) and the box streams:

| Address | Type | When |
|---|---|---|
| `/neon/tempo` | float BPM | on change ≥ 0.05 BPM, ≤ 4/s |
| `/neon/playing` | int 0/1 | on transport edges |
| `/neon/beat` | int 1..quantum, 0 stopped | each beat |
| `/neon/peers` | int | on change, ≤ 1/s |

Deltas compare against the last *sent* value, so slow drifts still
arrive; a failed send never suppresses the retry.

## Notes

- One UDP socket, one low-priority task on core 0. The real-time
  clock path is untouched — OSC just pushes the same commands the
  encoder does.
- IPv4 literal targets only (no resolver on the telemetry path).
- The beat message is timed by the sampling loop (20 ms), not by the
  pulse engine: use the TRS/DIN clock for anything that must be
  sample-tight, and `/neon/beat` for visuals.
