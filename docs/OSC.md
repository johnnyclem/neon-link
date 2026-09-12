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

## Ableton Live scene fire (outbound)

Link cannot launch Session View scenes. The box sends a Wi-Fi OSC bang
to **AbletonOSC** on the Live machine (no DIN / TRS / USB MIDI):

| Address | When |
|---|---|
| `/live/scene/fire_selected` | Next scene: RLCD **KEY** tap in OSC mode, or **KEY+BOOT** tap in standard mode. `POST /api/scene?op=next`. Fires the highlighted scene and selects the next row. Past the last scene: `/live/song/stop_all_clips` (and local stop). |
| `/live/scene/fire` *index* | Previous scene: RLCD **BOOT** tap in OSC mode. `POST /api/scene?op=prev`. At scene 1: stop. |
| `/live/song/stop_all_clips` | Walked off either end of the scene grid. |

Hold **KEY+BOOT 3 s** to switch the RLCD between **standard** (tempo / tap play-stop) and **OSC** (scene walk). In OSC mode, a short **KEY+BOOT** tap is quantized play/pause; KEY hold still opens the settings menu. Orientation is THEME menu / web editor only.

The repo vendors [AbletonOSC](https://github.com/ideoforms/AbletonOSC)
as a git submodule at `AbletonOSC/`. On this machine it is symlinked
into `~/Music/Ableton/User Library/Remote Scripts/AbletonOSC`. Restart
Live and pick **AbletonOSC** as a Control Surface (Link / Tempo / MIDI).
It listens on **UDP 11000**. Set this box's OSC **Send target** to
`LIVE_PC_IP:11000` (web editor System page, or `PUT /api/config` with
`{"osc":{"target":"192.168.50.10:11000"}}`). Inbound OSC can stay off
— a target is enough for scene bangs.

Live's global clip quantization (usually 1 bar) times the launch.
A next bang fires the highlighted scene and selects the next row; a
prev bang fires the previous index. Walking off either end of the
grid stops all clips.

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
