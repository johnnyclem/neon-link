# SPDIF jack characterization — bench procedure

**Question:** can the AMYboard's two SPDIF jacks be repurposed as general
clock/gate/CV I/O for the friends & family batch?

**Verdict (2026-08-16):** **No. Coupling caps. Leave them labeled SPDIF.**
Step 0 (published schematic) answers the question; Steps 1–5 are not needed.

Source schematic: `tulipcc/docs/pcbs/amyboard/amyboard-v1.4.sch`
(Eagle, "PCM9211 ADC and SPDIF" sheet). Same path in v1.0 and v1.1.

| Jack | Path | Coupling |
|---|---|---|
| **SPDIF IN** tip | 75 Ω to GND (`R65`) + ESD diode (`D16`) + **100 nF series** (`C1`) → PCM9211 `RXIN0` | AC |
| **SPDIF OUT** tip | PCM9211 `MPO0` → **100 nF series** (`C4`) → 150 Ω (`R3`) → tip | AC |

Neither tip is an ESP32 GPIO. Both are the PCM9211's S/PDIF analog front
end, AC-coupled by 100 nF as the part's application circuit requires.
A gate held high for a bar decays; Tempo CV becomes an edge blip. Decision
table row: **Coupling cap → audio-only. Not usable for clock, gate, or CV.**

Recorded in `docs/FRIENDS_FAMILY_HANDOFF.md` §3 D2 and the jack-field
copy (`JackField.tsx`, editor, site).

---

**Decided by two things:** whether the tip is plain GPIO, and whether the
path is AC-coupled. The second one is the killer — S/PDIF is AC-coupled by
spec, and a coupling capacitor makes DC-level signals impossible regardless
of what else is true.

**Time:** 30 minutes, plus 15 if you go to the powered test. (Skipped —
schematic was findable.)

---

## Step 0 — check for a schematic first (10 minutes, do this before probing)

AMY / Tulip is open hardware. If the AMYboard schematic or a pin map is
published, it answers every question below in ten minutes and you can skip
straight to Step 5.

Look for the SPDIF section: does the jack tip go to a module pin directly, or
through a series R, a divider, a cap, or a transformer?

Probe only if the schematic isn't findable.

**Done.** `docs/pcbs/amyboard/amyboard-v1.4.sch` in shorepine/tulipcc.
Tips go through 100 nF series capacitors into/out of the PCM9211, not to
an ESP32 GPIO. Stop here.

---

## Step 1 — safety and setup

- **Power off, USB disconnected** for everything through Step 4. All
  measurements are continuity and resistance on an unpowered board.
- DMM in continuity mode, then resistance, then capacitance if it has it.
- **Establish ground reference:** verify continuity between a Thonkiconn
  sleeve and a known ground point (USB shell or a labeled GND pad). Every
  measurement below is tip-to-ground unless stated.

Thonkiconn (PJ398SM) has three lugs: **tip**, **sleeve/ground**, and a
**normalling switch** contact that connects to the tip when nothing is
plugged in. Identify all three before measuring — probing the switch lug and
thinking it's the tip will send you down a false path.

---

## Step 2 — tip to ground, unpowered (the fast discriminator)

Measure resistance from SPDIF OUT tip to ground:

| Reading | Most likely |
|---|---|
| **OL / very high** | Direct GPIO, or series cap. Continue to Step 3. |
| **A few ohms** | **Transformer winding.** Stop — repurposing isn't worth it. |
| **~10–100 Ω** | Shunt leg of a divider (S/PDIF wants ~0.5 Vpp). Series R present; continue. |
| **Slowly climbing, doesn't settle** | **Series capacitor charging.** Strong AC-coupling signal. Confirm in Step 4. |

Repeat for SPDIF IN. The two may not be built the same way — an input needs
different conditioning than an output.

*Schematic prediction if anyone still probes: IN tip-to-ground ≈ 75 Ω
(`R65`). OUT tip-to-ground is the far side of `C4`/`R3` and should look
open or climb.*

---

## Step 3 — find the GPIO

Continuity from the jack tip to each accessible pin on the module's headers
or castellations.

- **Beeps, < 1 Ω** → direct GPIO. Best case. Record the pin number.
- **Fixed resistance (100 Ω, 330 Ω, ~300 Ω)** → series resistor. Still
  workable; record the value and the pin.
- **Nothing anywhere** → either the pin isn't broken out, or there's a cap or
  active part in between. Go to Step 4.

Note both pin numbers. You'll need them for Step 5 and for the firmware
either way.

---

## Step 4 — capacitor check

If your DMM has capacitance mode, measure tip to the GPIO pin found in Step
3 (or to ground if Step 3 found nothing). Anything in the **nF to µF** range
is a coupling cap.

Without capacitance mode: in resistance mode, a series cap reads as a value
that **climbs steadily and never settles** as the cap charges through the
meter. A resistor settles immediately. That drift is the tell.

**If there's a coupling cap, that's the answer: no DC-coupled signals.** A
gate held high for a bar decays to zero. Tempo CV — a slowly-varying DC level
— is the worst case and produces an edge blip instead of a level. Stop here,
label them SPDIF, move on.

---

## Step 5 — powered confirmation (only if Steps 2–4 look DC-coupled)

The definitive test. Flash a minimal sketch that drives the candidate GPIO
high for 5 s, low for 5 s, forever. Meter on the tip.

| Behavior | Verdict |
|---|---|
| Holds a steady level for the full 5 s | **DC-coupled. Usable.** Record the high level — 3.3 V, or lower if divided. |
| Jumps then decays toward 0 | AC-coupled. Confirms Step 4. Dead end. |
| Nothing | Wrong pin, or the path is only driven by a peripheral rather than the GPIO matrix. |

Scope version is better if it's already on the bench: 1 Hz square, watch for
droop across the high period. Droop = coupling cap, even a large one.

---

## Decision table

| Finding | What you get |
|---|---|
| Direct GPIO, DC-coupled, both jacks | Two general-purpose jacks. Best case. |
| Series R, DC-coupled | Same, but check the level under load — a divider may not swing high enough for Eurorack gate thresholds (~2 V typical, some gear wants 3 V+). Measure into a 100 kΩ load. |
| Coupling cap | Audio-only. Not usable for clock, gate, or CV. **← this batch** |
| Transformer | Leave it alone. |

---

## If it's usable: what to actually do with it

Not this batch. The output is not a GPIO; the input is not a GPIO. There
is no firmware pin to reassign to `clocks[1]`.

---

## Record the result

Whatever you find, write it into `HANDOFF.md` §3 D2 and into the strings in
`JackField.tsx` — right now they say "AMYboard hardware, unused by NEON
LINK," which will be wrong in one direction or the other by the end of this.

**Done.** `docs/FRIENDS_FAMILY_HANDOFF.md` §3 D2; `web/src/components/JackField.tsx`,
`web/src/routes/Outputs.tsx`, `plugin/Source/ui/Pages.cpp`, `site/index.html`.
