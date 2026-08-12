// GENERATED FILE - do not edit.
// Source: design/tokens.json, design/strings.json, design/icons.txt, design/fonts/hero.json
// Regenerate: python3 scripts/gen_design.py

/*
 * The 8x8 1-bit masters, verbatim. The web renders these as crisp SVG
 * pixel grids rather than redrawing them as vectors, so the icon on the
 * page is pixel-for-pixel the icon on the panel.
 */
export const iconMasters = {
  "link": [
    "........",
    ".##..##.",
    "#..##..#",
    "#.#..#.#",
    "#.#..#.#",
    "#..##..#",
    ".##..##.",
    "........"
  ],
  "wifi-ap": [
    "........",
    "#.#..#.#",
    ".#.##.#.",
    "..####..",
    "...##...",
    "...##...",
    "...##...",
    "........"
  ],
  "wifi-sta": [
    "........",
    ".######.",
    "##....##",
    "..####..",
    ".##..##.",
    "........",
    "...##...",
    "........"
  ],
  "ble": [
    "...#....",
    "...##...",
    "#.#.#...",
    ".###....",
    ".###....",
    "#.#.#...",
    "...##...",
    "...#...."
  ],
  "play": [
    "........",
    ".##.....",
    ".####...",
    ".######.",
    ".######.",
    ".####...",
    ".##.....",
    "........"
  ],
  "stop": [
    "........",
    ".######.",
    ".######.",
    ".######.",
    ".######.",
    ".######.",
    ".######.",
    "........"
  ],
  "run": [
    "........",
    "..####..",
    "..#..#..",
    "..#..#..",
    "###..###",
    "........",
    "........",
    "........"
  ],
  "warning": [
    "........",
    "...##...",
    "..#..#..",
    "..####..",
    ".#.##.#.",
    ".#....#.",
    "#..##..#",
    "########"
  ],
  "check": [
    "........",
    "......#.",
    ".....##.",
    "#...##..",
    "##.##...",
    ".####...",
    "..##....",
    "........"
  ]
} as const;

export type IconName = keyof typeof iconMasters;

export const ICON_SIZE = 8;
