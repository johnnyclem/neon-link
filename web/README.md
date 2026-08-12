# NEON LINK — web configuration interface

The page the module serves from its own HTTP server, at `http://neon-link.local/`
or `http://192.168.4.1/` during setup. Preact + TypeScript, built by Vite into a
single inlined HTML file and gzipped into the firmware component.

See [`../DESIGN_SYSTEM.md`](../DESIGN_SYSTEM.md) for the visual and interaction
rules this implements — in particular §14, which explains why the token, string,
icon and numeral files under `design/` must not be edited here.

## Develop

```bash
npm ci

# Against a real module (defaults to the setup AP):
NEON_DEVICE=http://10.0.0.42 npm run dev

# Against a stand-in, no hardware needed. The tempo and phase advance, so the
# status strip can be judged in motion:
npm run build && node scripts/mock-device.mjs      # http://localhost:8123
MOCK_SETUP=1 node scripts/mock-device.mjs          # first-boot wizard
```

## Build

```bash
npm run build              # typecheck, bundle, gzip into the firmware component
npm run build:styleguide   # dist-styleguide/styleguide.html — review artifact
```

`npm run build` writes `components/web_ui/www/dist/index.html.gz`, **and that file
is committed**. `idf.py build` has to work from a bare checkout with no Node
toolchain — which is what CI and anyone flashing the module does — so the
compressed artifact ships in the repo. CI rebuilds it and fails if the committed
copy has drifted, so "committed" never turns into "stale".

Rebuild and commit the artifact in the same commit as any change under `web/`.

The build fails if the gzipped bundle exceeds 60 kB. That is a design budget, not
a flash limit (the app partition has 3 MB free): it exists so a careless
dependency shows up as a failed build rather than as a slow first load over a
2.4 GHz link the user is standing on mid-setup.

## Layout

```
src/
  design/          GENERATED from ../design — never edit by hand
  styles/
    tokens.css     GENERATED
    base.css       the component layer
  components/      Icon, HeroTempo, PhaseBar, StatusChip, StatusStrip, controls
  routes/          Live, Outputs, Network, Midi, System, Setup
  styleguide/      review-only; excluded from the device build
  api.ts           typed wrappers over the module's four endpoints
```

The style guide has its own entry point and its own Vite config. That is what
guarantees the screen fixtures and swatch pages cannot end up in flash.

## Constraints worth knowing

- **No network at runtime.** The module serves this page from an access point with
  no internet. Nothing may reference a CDN — no webfonts, no external scripts. All
  assets are inlined.
- **One request.** `vite-plugin-singlefile` inlines the JS and CSS so the ESP32's
  HTTP server answers with one response.
- **Phone-first.** The realistic user is standing next to a rack holding a phone in
  a dim room. Targets clear 44 px, the save bar is sticky, the nav scrolls.
