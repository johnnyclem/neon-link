# Link Audio quality debugging

**Symptom under investigation**: very crackly, distorted, low-quality audio
while a Link Audio subscription (or publish) is active.
**Related**: [`AUDIOLINK.md`](AUDIOLINK.md) §5, §12.

This guide maps each audible failure mode to the counter that moves when it
happens, and gives the shortest experiment that separates them. All counters
are visible two ways: the `audio:{}` block of `GET /api/status`, and a
rate-limited `audio_svc` log line printed every ~5 s while streaming:

```
la: state 2 fill 61 ms trim -38 ppm | d5s rx_drop 0 jit_drop 0 rebuf 0 i2s_und 0 tx_drop 0 | clk 12 ppm
```

plus a `link_audio` line with the receive ring's high-water mark:

```
rx ring high-water 9/128, dropped 0
```

## The receive pipeline, and where each loss counter lives

```
Link network thread (core 0)
  │  on_receive: beat-stamp, push
  ▼
AudioBlockRing (kRxSlots blocks)          ── rx_dropped   (overflow / oversize)
  │  audio task drains ≤32 blocks/5.3 ms
  ▼
JitterBuffer FrameRing (~680 ms, PSRAM)   ── sub_dropped  (drop-oldest overflow)
  │  fill servo trims resampler ±500 ppm     jit_underruns (rebuffer events)
  ▼
LinearResampler → mixer → soft_clip → I2S ── underruns    (i2s write failures)
```

Publish direction: audio task → per-sink AudioBlockRing (`tx_dropped`) →
pump task (4 ms) → `LinkAudioSink` commit.

## Failure signatures

| What you hear | What moves | Root cause |
|---|---|---|
| Crackle / splices, fill bleeding down, periodic silence-then-recovery | `rx_dropped` climbs; rx high-water pegs at the ring size | WiFi delivers in bursts longer than the **block ring**, upstream of the jitter buffer. Raising `la_jitter_ms` cannot help — the audio is gone before the jitter buffer sees it. Deepen `kRxSlots` (done: 16 → 128) or move to direct-push (below). |
| Stutter: ~`jitter_ms` of silence, then audio, repeating | `jit_underruns` climbs, `rx_dropped` quiet | Genuine late packets or sender gaps; raise `la_jitter_ms` (this is what it is for), check RF environment, disable the SoftAP while testing (`APSTA` beacons cost airtime). |
| Continuous harmonic distortion, no dropouts, counters all quiet; drums/clicks fine, Rhodes/guitar crushed | nothing — peaks near 1000 | Was the mixer's `soft_clip` (knee 0.75) on the solo Link tap. Solo program taps now bypass it; the mix bus still has a 0.97 safety knee. If you still hear this on an old image: set subscribe gain to ≤150. |
| Pitch-stable but gritty top end on music | nothing | Linear-interpolation SRC operating at a non-1.0 ratio (sender rate ≠ 48 kHz, or servo trim active). Expected ceiling of the current resampler; windowed-sinc is the upgrade path. |
| Everything distorted, including the local metronome / synth | nothing | Not Link Audio at all: I2S slot format vs the PCM3060 strap (32-bit left-justified vs standard I2S one-BCLK delay). Verify with the metronome alone; if it is dirty too, fix `i2s_audio.cpp` slot config first — no network test is meaningful until the local path is clean. |
| Dead air, Live shows no Link Audio channel at all, `audio_svc` never logs `Link Audio enabled` | nothing — `publishing` stays 0, no `la:` line | Control loop cached `enableLinkAudio` on a null session (link_svc still in the STA wait). Fixed: wait for `session_ready()` before caching. Reflash; confirm the log line after WiFi associates. |
| Dead air, `sub_state` stuck buffering | `rx_dropped` climbs fast; log says `rx block exceeds 512 frames` | Sender block size above `kMaxBlockFrames`; every block is dropped. Reduce the sender's buffer size or raise `kMaxBlockFrames`. |
| Crackle in *Live* while it monitors the module | `tx_dropped` climbs | Pump task starved or WiFi TX congested; check core-0 load (HTTP polling, OLED). |
| Fill ramps steadily against a pinned ±500 ppm trim | `trim_ppm` pegged | Real clock offset beyond servo authority — check `sub_rate` matches what the sender claims, and `clk ppm` for SampleClock trouble. |

## Diagnostic order

1. **Local path first**: metronome only, no subscription. Clean? Then the
   codec/I2S config is fine and everything below is network-side.
2. **Subscribe, watch the 5 s log line** for one minute. The first counter
   that moves is the diagnosis — the table above is in rough likelihood
   order for a busy 2.4 GHz studio LAN.
3. **`rx ring high-water`** near `kRxSlots` with drops = burst overflow
   (the pre-jitter-buffer loss that motivated this branch's fixes).
4. **Gain A/B** (`la_sub_gain` 200 → 150) any time "distorted" is part of
   the complaint — it is free and rules the saturator in or out.
5. Test STA-only (SoftAP off) and with BLE MIDI idle before blaming
   anything in this codebase: 2.4 GHz airtime is the whole game.

## Known sharp edges in the current design

- **The block ring counts blocks, not time.** Its depth in milliseconds is
  `slots × sender_block_frames / rate`, so a sender using small blocks gets
  a much shallower ring than one using 512-frame blocks. The durable fix is
  pushing received blocks straight into the jitter buffer's frame ring from
  the network callback (it stays SPSC — network thread producer, audio task
  consumer) so buffering capacity is measured in frames; the deeper block
  ring is the low-risk interim.
- **The jitter buffer does not trim overshoot on start**: fill collected
  above target while buffering stays as extra latency until the ±500 ppm
  servo grinds it off (~33 s per 100 ms of excess). Latency complaint, not
  a quality one.
- **Beat stamps are carried but not used for splice concealment**: a lost
  block is spliced hard, which is the "crackle" sound. Any packet loss is
  audible; the counters tell you it happened, not make it inaudible.
- **`kDacLatencyUs` is still 0** — sync-to-Live offset calibration
  (AUDIOLINK.md PR8) has not happened; irrelevant to crackle but visible as
  constant skew against Live's click.
