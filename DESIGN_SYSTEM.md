# NEON LINK — Design System

**Version**: 1.0
**Status**: Active
**Applies to**: Web configuration interface + 128×128 monochrome on-device display
**Hardware context**: AMYboard prototype (128×128 panel, rotary encoder pending)

---

## 1. Design Intent

The web UI and the device display must feel like **two surfaces of the same object**.

- The device is the live instrument face.
- The web UI is the expanded control surface and setup desk.
- Shared hierarchy, shared language, shared visual DNA.
- Constraints of the 128×128 mono display are treated as a feature, not a limitation — they force clarity that the web UI inherits.

**Personality**

- Confident, minimal, slightly retro-digital
- "Neon" as attitude more than literal glow
- 90s cool without pastiche (Saved by the Bell energy filtered through a modern instrument)
- Readable at a glance in a dark modular case
- Never cute, never corporate, never skeuomorphic

---

## 2. Shared Information Hierarchy

Both surfaces obey the same ranking of information:

| Priority | Content | Device treatment | Web treatment |
|----------|---------|------------------|---------------|
| 1 (Hero) | Tempo (BPM) | Largest possible digits | Large, always visible header |
| 2 | Link / transport state | Short status line | Clear status chip + transport |
| 3 | Network identity | IP or `AP` / `STA` | Full network panel |
| 4 | Secondary status (BLE, phase) | Icons or short labels | Detailed panels |
| 5 | Configuration | Nested menus via encoder | Full forms & advanced pages |

**Rule**: If it is important enough to show on the device, it must be instantly findable and visually consistent on the web.

---

## 3. Visual Language

### 3.1 Colour (Web)

Dark-first interface. Neon accents are used sparingly and with purpose.

| Token | Hex | Usage |
|-------|-----|-------|
| `--bg` | `#0B0C0F` | Page background |
| `--surface` | `#14161A` | Cards, panels |
| `--surface-2` | `#1C1F26` | Elevated / selected |
| `--border` | `#2A2E38` | Hairlines, dividers |
| `--text` | `#E8EAED` | Primary text |
| `--text-muted` | `#8B909A` | Secondary labels |
| `--neon` | `#00F0FF` | Primary accent (Link active, focus, key actions) |
| `--neon-dim` | `#00A8B3` | Secondary accent / hover |
| `--magenta` | `#FF2D95` | Alerts, BLE active, special states |
| `--yellow` | `#F5C518` | Warnings, "attention" |
| `--success` | `#3DFF9A` | Connected / running |
| `--danger` | `#FF4D4D` | Errors, disconnect |

**Accent rule**: Neon cyan is the "system is alive / Link is present" colour. Magenta is reserved for wireless/BLE personality. Do not rainbow the UI.

Contrast targets for every text/background pair are declared alongside the palette in `design/tokens.json` and asserted by `scripts/check_contrast.py` in CI. Changing a colour without meeting its target fails the build rather than quietly degrading legibility.

### 3.2 Monochrome (Device 128×128)

Pure 1-bit. No grayscale dithering for core UI (except optional progress fills).

| Element | Treatment |
|---------|-----------|
| Background | Black (0) |
| Primary content | White (1) |
| Secondary / muted | White, lower density (shorter labels, thinner lines) |
| Active / selected | Inverted block or strong outline |
| Progress / phase | Solid bar or sparse tick marks |
| Icons | Geometric, 1px or 2px stroke, no antialiasing |

**Density rule**: Prefer empty space over decoration. A 128×128 display rewards restraint. The live screen keeps a deliberate empty band between the identity line and the phase bar; it is not available for reuse.

**Where dither is allowed**: `Framebuffer::fill_rect_dither` exists for progress and meter fills only, and must never sit behind text. Its one production use today is the phase bar when the transport is stopped — a half-tone fill reads as "held, not advancing", which is information rather than decoration.

### 3.3 Bridging the two

Three things are drawn from a single definition rather than implemented twice:

- **The numerals.** The web hero tempo is not a font resembling the device's — it renders the same seven-segment map from `design/fonts/hero.json` as SVG. The browser adds the one thing a 1-bit panel cannot: unlit segments, shown as ghosts.
- **The icons.** Authored once as 8×8 1-bit masters in `design/icons.txt`. The web draws the same master as a crisp SVG pixel grid instead of redrawing it as a vector, so the icon on the page is pixel-for-pixel the icon on the panel.
- **The phase bar.** Drawn in the device's own coordinate space — 128 units wide, the same inset, the same beat ticks, never stretched. Both surfaces derive the phase from one function, `neon::phase_milli_beats`, so they cannot disagree about where the bar is.

The status vocabulary (`LINK`, `STOP`, `AP`, …) likewise comes from one file, with a panel abbreviation and a web phrasing per state.

---

## 4. Typography

### Web

- **UI / labels**: system UI stack (clean, slightly technical)
- **Hero BPM**: the seven-segment face, shared with the device
- **Mono / data**: system monospace with `font-variant-numeric: tabular-nums` for IPs, versions, raw values
- Scale: clear hierarchy (hero → title → body → caption)

Nothing is fetched from a CDN. The module serves this page from its own access point with no internet, so a webfont would have to be vendored into flash; the system stack costs zero bytes and never fails to load. Vendoring a subsetted face remains an option — `--font-mono` already names `NEON Mono` first as the hook for it.

### Device (128×128)

- Large BPM: **16×26 seven-segment numerals**, covering only `0-9 . : -`
- Status line and menu items: 5×7 at 1×
- Values that must read across a room: 5×7 at 2×
- No anti-aliasing on the mono panel — crisp pixels only
- All caps for status tokens (`LINK`, `STOP`, `AP`)

**Shared rule**: Numbers that represent tempo or timing always use tabular figures so digits don't jump. The seven-segment face is tabular by construction: `1` advances the same width as `8`, which is why the tempo readout does not jitter as it changes.

---

## 5. Layout Principles

### Device (128×128)

```
┌────────────────────────────┐
│ NEON              [icons]  │  ← brand + status icons
│ ────────────────────────── │  ← header rule
│                            │
│         128.0              │  ← hero BPM (seven-segment)
│          BPM               │
│  LINK  RUN  STA  2P        │  ← state row
│      10.0.0.42             │  ← network identity
│                            │  ← deliberate empty band
│ ████████░░░░░░░░░░░░░░░░░░ │  ← phase / progress
└────────────────────────────┘
```

Every offset comes from `design/tokens.json` via `neon/ui/theme_gen.hpp`. Screens compose the primitives in `neon/ui/widgets.hpp`; no screen may hard-code a pixel coordinate.

### Device — compact (128×64)

Native SSD1306/1309 panels (the Daisy Seed target, docs/DAISY.md §4) get a second vertical flow from the same design system — `device_compact` in `design/tokens.json`, emitted as `ui::kLayout64` and rendered by the same pure `render_ui()`:

```
┌────────────────────────────┐
│ NEON              [icons]  │  ← same header band as the big layout
│ ────────────────────────── │
│         128.0              │  ← hero BPM, same seven-segment face
│  LINK  RUN  STA  2P        │  ← state row
│ ████████░░░░░░░░░░░░░░░░░░ │  ← phase / progress (8 px)
└────────────────────────────┘
```

Two bands are dropped rather than squeezed (`unit_y` / `ident_y` = −1): the hero is self-evidently a tempo, and the panels that ship this layout have no network identity to show. Lists keep the exact 12 px row pitch — four rows instead of nine — and the density rule survives the halving: nothing gets a smaller font. The compact flow renders into the **top half** of the shared 128×128 framebuffer (rows 64–127 provably blank, asserted by the host suite), so a 64-row panel's flush is pages 0–7 verbatim. Both geometries of every fixture live side by side in `design/screens.json` and the style guide.

**Encoder navigation model**

- Rotate: move focus, or adjust the focused value
- Short press: enter / confirm / toggle
- Long press: cancel an in-progress edit; if there is none, go back one level
- Focus while browsing is a caret; focus while editing is a full-row inversion, so the knob's mode is never ambiguous
- Menus are vertical lists; the tree is one level deep

### Web

- Dark shell, centred content, max-width 960 px
- Live status strip always visible (tempo + phase + transport + network)
- Setup wizard (first boot / AP mode) is linear and calm, one thing per step
- Configuration split into Live, Outputs, Network, MIDI / BLE, System
- Mobile-first: this gets used from a phone while the module is in a case, so every target clears 44 px and the save bar follows you down long forms

---

## 6. Component Mapping (Web ↔ Device)

| Concept | Device | Web |
|---------|--------|-----|
| Tempo | Huge centred seven-segment number | Same numerals as SVG, with ghost segments |
| Link state | `LINK` / `EXT` text + icon | Status chip |
| Transport | `RUN` / `STOP` | Status chip |
| Network mode | `AP` / `STA` / `ETH` + IP | Network card with mode + IP + actions |
| Phase / beat | Horizontal bar | Same bar, same proportions |
| BLE | `BLE` icon in the header | Toggle + status chip |
| Output assignment | Menu list | Card per output |
| Focus / selection | Caret, or inverted row while editing | Focus ring in neon |
| Destructive action | Confirm screen (defaults to NO) | Confirm modal |

Shared labels (do not invent synonyms): `LINK`, `EXT`, `STOP`, `RUN`, `AP`, `STA`, `ETH`, `BPM`, `BLE`, `MIDI`, `CV`, `CLK`, `RST`.

---

## 7. Icon System

Icons are authored once as pure 1-bit masters and used unchanged by both surfaces.

Set: `link`, `wifi-ap`, `wifi-sta`, `ble`, `stop`, `run`, `warning`, `check`.

Four of them animate — see §8 for which clock each uses and why. An icon that nothing references is dead weight and gets deleted rather than kept "just in case"; `play` went that way when `run`'s marching gate turned out to be the better mark for a running transport.

Style: geometric, closed shapes where possible, consistent stroke weight, no filled blobs unless needed for recognition at 8 px.

They are authored at **8×8** rather than 16×16, because 8×8 is the size the device header actually renders. The web scales the same master to 16 or 32 px as a pixel grid. Pixel-art icons at 2×/3× are a deliberate style here, not an artefact.

### Judge them at 1×

`design/icons_preview.png` is generated alongside the code and shows every icon at 1×, 2×, 4× and 8×, in the same order as `design/icons.txt`. **A glyph that does not read in the first two columns does not work**, whatever it looks like blown up.

This exists because two icons shipped unreadable before it did, both for the same reason: they were only ever reviewed enlarged. An 8×8 master viewed at 32 px tells you nothing about whether it survives in the panel header.

### What 8×8 can and cannot carry

8×8 gives you roughly twenty meaningful pixels of outline and no usable interior. That supports **silhouettes** — shapes recognised from their outline — and rules out **pictograms**, which are recognised from internal structure.

Three failed attempts at the error icon all hit this same wall, and are worth not repeating:

| Attempt | Why it failed |
|---|---|
| Outline triangle with an exclamation inside | The 1px walls close up against the bang; reads as a seated figure |
| Solid triangle with the exclamation knocked out | The tapering silhouette reads as a fir tree |
| X inside a circle | A circle outline leaves a 4×4 interior, and a 4×4 X has a 2×2 centre — the strokes merge into a square hole, giving a ring with a box in it |

A container **or** interior detail, never both. The resolution is to drop the container and let the mark use the whole cell.

---

## 8. Motion & Feedback

**Motion is meaning.** Something moves only while the thing it stands for is actually happening. That is the whole rule; everything below follows from it.

The test for any proposed animation is: *if this stopped moving, would I have lost information?* A pulsing Link ring says the session is alive and how fast — stop it and you have lost that. A spinning decoration says nothing — stop it and you have lost nothing, which means it should never have moved.

### Two clocks, and why it matters which

Icon loops declare their own clock in `design/icons.txt`, because the two kinds of motion mean different things:

| Clock | Advances | For |
|---|---|---|
| `beat` | Once per musical beat, from the shared phase | Anything tempo-locked: the Link pulse, the run gate |
| `tick` | A fixed 5 Hz counter | Motion with no musical time: a radio beaconing, a stack advertising |
| *(none)* | Never | Everything else — still the right answer for most icons |

`beat` is the one that only makes sense on a clock module. Those loops speed up when the tempo does, so the panel visibly *runs* at the tempo rather than reporting it as a number. It is the reason to have animation here at all; a fixed-rate spinner would be generic UI polish.

Beat-locked loops **freeze at frame 0 while the transport is stopped**. The Link timeline keeps advancing whether or not anything is playing, so without this the run gate would march through a bar that is not happening.

### What holds still

Settled states do not move, and this is a rule rather than an oversight:

- `stop` — a stopped transport that animates is a contradiction
- `check` — a confirmation is a moment, not a state
- `warning` — a steady error is easier to read than a flashing one

### Device

- Instant response to the encoder
- Hard cuts only: no fades, no tweening — a 1-bit panel cannot fade, so the vocabulary does not pretend otherwise
- 3–6 frames per loop. The UI task flushes at 10 fps whether or not anything changed, so animation costs no extra bus time, but it also caps the frame rate
- Loop state arrives in `UiStatus`, never from a clock inside the renderer. `render_ui()` stays pure, so the golden tests still mean something now that the panel moves

### Web

- The same frames, played the same way: a `steps()` animation over a filmstrip, which is a hard cut per frame exactly like the panel
- Beat-locked loops run at the tempo the page is already polling
- Subtle, fast transitions elsewhere (150–200 ms)
- Never decorative motion
- `prefers-reduced-motion` parks every loop on frame 0, which each icon is drawn to survive, rather than collapsing the duration and landing on an arbitrary frame

---

## 9. Content & Tone

- Short, direct labels
- Status tokens on the device; the web may be slightly more explanatory but still terse
- Error messages state the problem **and the next action**
- No marketing copy inside the product UI

Examples:

- Device: `LINK STOP AP`
- Web: "Link · Stopped · Setup AP"
- Device error: `NO LINK`
- Web error: "Not connected to a Link session. Check Wi-Fi or start a session in Ableton."

Wi-Fi failures are translated from ESP-IDF disconnect reasons into actionable sentences ("Handshake timed out — usually a wrong password") rather than surfacing a raw number.

---

## 10. Setup Flow Alignment

First-boot / AP mode is the strongest moment of cohesion.

**Device**

- Large BPM, or `--.-` until synced
- `AP` plus the address (`192.168.4.1`)
- Phase indicator running

**Web** (opened at that address)

- Immediately mirrors the same tempo and state
- Same accent language, same status vocabulary
- Three steps: Wi-Fi credentials → confirm the panel shows `STA` → optional BLE

After setup, returning to the web UI should feel like opening a larger version of the same instrument, not a different product.

---

## 11. Encoder Interaction (Device)

| Action | Behaviour |
|--------|-----------|
| Rotate | Move focus, or change the focused value |
| Short press | Enter / confirm / toggle |
| Long press | Cancel an edit; otherwise back one level |

Menu structure stays shallow:

```
Live
  Menu ─ Live · Outputs · Network · MIDI · System
           Outputs ─ CLK 1..4 ─ parameter list
           Network ─ read-only (credentials need a keyboard)
           MIDI    ─ parameter list
           System  ─ parameter list ─ Reboot ─ Confirm
```

The long press fires as soon as the hold threshold passes rather than on release, so the panel reacts under the finger.

**Tempo is not editable from the live screen.** It is reachable through the menu instead. The live path stays fast and boring, and an encoder that changes tempo on an accidental nudge is a hazard on stage.

---

## 12. Accessibility & Practical Constraints

- The device must remain readable in low light and at arm's length in a rack
- The web must work on a phone in a dim studio
- High contrast is non-negotiable on both, and is enforced in CI
- Do not rely on colour alone for state — every chip pairs colour with a word and an icon, and the device toggle states spell out `ON` / `OFF`

---

## 13. Implementation Notes

### Web

- CSS custom properties for every token
- System fonts only
- Preact, no heavy framework
- Live values update by polling `/api/status`, never a full reload

### Device

- Single source of layout constants: `neon/ui/theme_gen.hpp`
- Reusable draw primitives in `neon/ui/widgets.hpp`: `draw_label`, `draw_hero_bpm`, `draw_status_row`, `draw_bar`, `draw_focus`, `draw_header`, `draw_list_row`, `draw_confirm`
- The live screen code path stays fast and boring

### Shared

- One dictionary of status strings, consumed by both
- Versioned with the firmware and web releases

---

## 14. The Pipeline

Nothing in this system is maintained in two places. `design/` is the source of truth; `scripts/gen_design.py` emits every downstream artifact.

```
design/tokens.json ─┐
design/strings.json ├─→ scripts/gen_design.py ─┬─→ web/src/styles/tokens.css
design/icons.txt   ─┤                          ├─→ web/src/design/{tokens,strings,icons,heroFont}.ts
design/fonts/hero.json ─┘                      ├─→ neon/ui/theme_gen.hpp
                                               ├─→ neon/ui/icons_gen.hpp
                                               ├─→ neon/ui/hero_font_gen.hpp
                                               └─→ design/fonts/hero_preview.txt

host/sim/neon_screens.cpp ──(the firmware's own render_ui)──→ design/screens.json
                                                                    │
                                                                    └─→ the style guide's simulator
```

All generated files are committed, and CI regenerates them and fails on any diff. The rules:

- **Never hand-edit a generated file.** Each carries a header saying so.
- Change a colour, a layout constant, a status word, an icon or a numeral in `design/`, then run `python3 scripts/gen_design.py`.
- `design/fonts/hero_preview.txt` exists so a change to the segment geometry shows up as readable pixels in the diff instead of a wall of hex.

### Reviewing without hardware

```bash
cmake -S host -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j
./build-host/neon_screens > design/screens.json     # capture every panel screen

cd web && npm ci
npm run build:styleguide                            # dist-styleguide/styleguide.html
```

The style guide renders the captured panel screens at 1:1 pixel fidelity beside the web components that mirror them. Those frames come from the firmware's own `render_ui()`, so it shows what the panel draws rather than a mockup of it.

---

## 15. Decisions Resolved

The open questions from the first draft, and where they landed:

| Question | Decision |
|----------|----------|
| Bitmap font for the 128×128 panel | A purpose-built 16×26 seven-segment face covering only `0-9 . : -`. The 5×7 UI font scaled 3× tops out at 21 px and reads as blown-up text; a segment readout reads as an instrument, is crisp by construction at 1-bit, and costs about 1 kB of ROM. |
| Tempo editable from the live screen? | No — via the menu only. Keeps the live path fast and boring, and avoids an accidental nudge changing tempo mid-set. |
| Final icon set and their 1-bit masters | Nine icons at 8×8, in `design/icons.txt`. |
| Should the web live strip echo the device's phase bar 1:1? | Yes, literally: the web bar uses the device's coordinate space and is never stretched. |

---

## 16. Current Prototype Alignment

The first hardware screen already expressed the right instincts — brand top-left, hero BPM, compact state row, IP identity, a single progress element. The system formalises and extends that language rather than replacing it: the layout is unchanged in spirit, but every offset now comes from a token, the numerals are purpose-built, and the same words and icons appear on the web.

When the encoder arrives, the same visual rules apply to menus and focused states. The navigation graph, the long-press gesture and every menu screen are already implemented and covered by host tests — the firmware is waiting on the knob, not on code.

---

**End of DESIGN_SYSTEM.md**

This document is the source of truth for visual and interaction consistency between the on-device UI and the web configuration interface.
