# S1 Phase A — scene launch observability

The critical unknown from [STEM_SYNC.md §7 S1](../../docs/STEM_SYNC.md).
Throwaway code whose only output is a decision. No hardware, no clients,
no audio, no Control Surface script of our own — AbletonOSC and a Python
daemon on the same laptop as Live.

## Question

When a scene is launched in Live, can we learn **which** scene and **what
beat it will start on**, early enough to get that to four clients over
WiFi?

Everything depends on observing the **queued** state
(`Track.fired_slot_index`), not the playing state. If only
`playing_slot_index` is observable, there is no lookahead by definition
and the design needs an external trigger source.

## The margin, stated correctly

The PRD claimed 2000 ms of slack from one bar at 120 BPM. That is the
maximum, not the typical. The real margin is

```
lookahead = target_beat_time − moment_of_click
```

and musicians launch scenes **on or just before the beat** — the worst
case is the normal case, not a tail event. Measuring the distribution of
that margin is the point of Phase A. Not "does it work" — "how much time
do we really get, and how often is it not enough."

The mitigation is decided in advance: a client whose launch event arrives
**after** the target beat seeks into the local file to where playback
would already be and joins in time with everyone else. Phase A tells us
how often that path runs — if the 10th percentile is 20 ms rather than
100 ms, the seek path is primary, not a fallback. That is a design
change, not a failure.

## What runs

- `daemon.py` — subscribes to AbletonOSC listeners, writes one CSV row
  per event. On every `fired_slot_index` change it snapshots the beat
  clock, computes the predicted target beat and lookahead, fetches the
  per-clip quantization override, and pairs the eventual
  `playing_slot_index` change back to the prediction.
- `analyze.py` — distribution stats, ASCII histogram (the A1 histogram
  **is** the result), and mechanical checks of P1–P4.

OSC addresses were read from the AbletonOSC README and source, not
guessed. Three findings from that reading, baked into the daemon:

1. **Get replies and listener pushes share an address**
   (`/live/<obj>/get/<prop>`) and are indistinguishable on the wire. The
   daemon treats both as cache updates.
2. **`current_song_time` pushes arrive at UI rate** (~60–100 ms), too
   coarse for a lookahead measurement on its own. While the transport
   runs, the daemon extrapolates from the last push using the tempo;
   stopped transport is never extrapolated.
3. **`Clip.launch_quantization` is get-only** (no listener), so it is
   fetched per trigger, with a 300 ms timeout falling back to the global
   quantum. The prediction snapshot is taken at fired-event receipt
   either way. Enum: `0` = follow global, `1` = none, `n≥2` = the song
   enum shifted by one.

## Reading the numbers

- `lookahead_ms` on `fired` rows is the headline distribution.
- `prediction_error_beats` on `playing` rows is a **raw** estimator taken
  at receipt of the playing event, so OSC delivery latency appears as a
  small positive error. P2 — the hard gate — is checked by `analyze.py`
  snapping the observed beat to the quantum grid and comparing grid
  points, not raw floats.
- A `playing` row marked `no_pending_fired` is a launch the trigger never
  led — a P1 failure, unless it is the A10 Follow-Action cell, where it
  is the documented limitation being probed.

## Pass / kill

| # | Criterion | Threshold |
|---|---|---|
| P1 | Listener fires before the target beat | ≥ 95% of launches |
| P2 | Predicted target beat matches actual start | **hard gate**, 100% |
| P3 | Median lookahead, 1 Bar / 120 BPM | ≥ 500 ms |
| P4 | p10 lookahead, 1 Bar | ≥ 100 ms |
| P5 | Per-clip override handled | 100% (judge from A5 file) |
| P6 | Live 11 ≡ Live 12 | yes (judge from A9 files) |

Kill: P2 fails; the listener only fires at/after playback start;
Live 11 and 12 differ materially; `fired_slot_index` is not observable
at all.

Do not report "passed." Paste the analyze output with the CSVs — the
distribution is the question.
