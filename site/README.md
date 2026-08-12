# NEON LINK — marketing page + interactive manual

`index.html` is a single self-contained page: the product pitch plus an
interactive manual for both the device and the web editor. No build step, no
CDN, no webfonts — the same zero-dependency rule the module's own web UI lives
by. Open it from disk or serve it from any static host (GitHub Pages works).

What's inside is real, not mocked:

- The **panel screens** are the 19 framebuffers from `design/screens.json`,
  captured from the firmware's own `render_ui()` and decoded to canvas at 1:1.
- The **seven-segment tempo** renders the segment map from
  `design/fonts/hero.json`, ghost segments and all.
- The **icons** are the 8×8 1-bit masters from `design/icons.txt`, drawn as
  SVG pixel grids.
- The **palette, spacing and radius(0)** come from `design/tokens.json`.
- The hero photo is a downscaled copy of `IMG_4635.jpeg` (concept render).

## Regenerating

The page embeds copies of those assets, so it does not update itself when
`design/` changes. If tokens, icons, numerals or screens change, re-embed them:
the data lives in one `const DATA = {...}` at the top of the page's script and
matches the source files' JSON verbatim (with `$comment` keys stripped, and
the screens reduced to `id`/`title`/`note`/`bits`).
