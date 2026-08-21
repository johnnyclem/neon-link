# S1 Phase A — RUNBOOK

**Read this whole file before clicking anything.**

Time box: 1 day. Deliverable: the CSVs, the A1 histogram, pass/fail
against the README table, and a recommendation — proceed to Phase B,
proceed with the seek path promoted to primary, or change the trigger
source.

## Setup

1. Install [AbletonOSC](https://github.com/ideoforms/AbletonOSC) as a
   Control Surface (copy into `Remote Scripts`, select **AbletonOSC** in
   Preferences → Link/Tempo/MIDI → Control Surface). Assign **no MIDI
   ports** — that is the point, and it demonstrates the transport
   argument for Phase B.
2. `pip install python-osc` (that is the only dependency).
3. Run the daemon **on the same machine as Live**. Measuring through a
   WiFi hop adds the network tail to every number and answers a
   different question.
4. A test set: a handful of tracks with clips in several scenes, 120 BPM,
   4/4. Content is irrelevant; empty MIDI clips are fine.

## Do not

- Do not run another OSC client on the machine — AbletonOSC replies to
  UDP **11001** and the daemon must own that port. It exits with a clear
  error if it cannot bind.
- Do not click through the matrix at one tempo of clicking. The
  instruction is to vary *where in the bar* you click — early, middle,
  late, and right on the line. The distribution is the result, and
  launches clustered mid-bar will flatter it.
- Do not report "it works." The lookahead distribution is the answer.

## Per cell

```
python3 daemon.py --cell A1        # writes s1_A1_<timestamp>.csv
```

20 launches per cell, deliberately varying click position in the bar.
Watch the live console: each launch prints its lookahead and, when the
clip starts, the raw prediction error. Ctrl-C between cells.

| # | Condition | Watching for |
|---|---|---|
| A1 | Global 1 Bar, click at varied points | **lookahead distribution** — the headline |
| A2 | Global 2 Bars | does more quantization buy proportional margin |
| A3 | Global 1/4 | where does it become unusable |
| A4 | Global **None** | confirm it is undetectable ahead of time |
| A5 | Per-clip override differing from global | `quantum_from=clip_q` in the notes, right quantum used |
| A6 | Scene launch vs individual clip launch | do both fire the listener |
| A7 | Transport stopped, then scene launched | `transport_stopped` note; what `current_song_time` reads before play |
| A8 | Tempo changed mid-session | does the ms conversion track (tempo rows appear in the CSV) |
| A9 | Live 11 vs Live 12 | same behaviour — rerun A1 on the other Live |
| A10 | Follow Actions triggering a clip | does an automatic launch fire `fired_slot_index`, or only `playing` with `no_pending_fired` |

A10 is worth the ten minutes: if Follow Actions fire the listener they
are a free feature; if not, that is a documented limitation.

## Afterwards

```
python3 analyze.py s1_A1_*.csv                 # per-cell
python3 analyze.py s1_A*.csv --png lookahead.png   # pooled + the chart
```

`analyze.py` prints P1 and P2 mechanically. P2 is the **hard gate**: a
single grid MISS line means the predicted beat was wrong and the
architecture is unsound as designed. P3/P4 are read off the A1 stats
line. P5/P6 are judged by a human from the A5 and A9 files.

Paste raw analyze output with the CSVs. "It works" without the lookahead
distribution is not a result.

## Explicitly out of scope

No hardware, no ESP32, no audio. No Control Surface script of our own
(Phase B, only if this passes). No network transport — UDP delivery is a
separate, already-measured problem. No stem export, format, transfer, or
mixing. No Windows, unless Live 11/12 on macOS diverge.
