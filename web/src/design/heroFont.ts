// GENERATED FILE - do not edit.
// Source: design/tokens.json, design/strings.json, design/icons.txt, design/fonts/hero.json
// Regenerate: python3 scripts/gen_design.py

export const heroFont = {
  "cell": {
    "width": 16,
    "height": 26
  },
  "tracking": 2,
  "segments": {
    "A": {
      "x": 0,
      "y": 0,
      "w": 16,
      "h": 4
    },
    "B": {
      "x": 12,
      "y": 0,
      "w": 4,
      "h": 15
    },
    "C": {
      "x": 12,
      "y": 11,
      "w": 4,
      "h": 15
    },
    "D": {
      "x": 0,
      "y": 22,
      "w": 16,
      "h": 4
    },
    "E": {
      "x": 0,
      "y": 11,
      "w": 4,
      "h": 15
    },
    "F": {
      "x": 0,
      "y": 0,
      "w": 4,
      "h": 15
    },
    "G": {
      "x": 0,
      "y": 11,
      "w": 16,
      "h": 4
    }
  },
  "glyphs": {
    "0": {
      "width": 16,
      "on": [
        "A",
        "B",
        "C",
        "D",
        "E",
        "F"
      ],
      "rects": []
    },
    "1": {
      "width": 16,
      "on": [
        "B",
        "C"
      ],
      "rects": []
    },
    "2": {
      "width": 16,
      "on": [
        "A",
        "B",
        "G",
        "E",
        "D"
      ],
      "rects": []
    },
    "3": {
      "width": 16,
      "on": [
        "A",
        "B",
        "G",
        "C",
        "D"
      ],
      "rects": []
    },
    "4": {
      "width": 16,
      "on": [
        "F",
        "G",
        "B",
        "C"
      ],
      "rects": []
    },
    "5": {
      "width": 16,
      "on": [
        "A",
        "F",
        "G",
        "C",
        "D"
      ],
      "rects": []
    },
    "6": {
      "width": 16,
      "on": [
        "A",
        "F",
        "G",
        "E",
        "C",
        "D"
      ],
      "rects": []
    },
    "7": {
      "width": 16,
      "on": [
        "A",
        "B",
        "C"
      ],
      "rects": []
    },
    "8": {
      "width": 16,
      "on": [
        "A",
        "B",
        "C",
        "D",
        "E",
        "F",
        "G"
      ],
      "rects": []
    },
    "9": {
      "width": 16,
      "on": [
        "A",
        "B",
        "C",
        "D",
        "F",
        "G"
      ],
      "rects": []
    },
    "-": {
      "width": 16,
      "on": [
        "G"
      ],
      "rects": []
    },
    ".": {
      "width": 6,
      "on": [],
      "rects": [
        {
          "x": 1,
          "y": 22,
          "w": 4,
          "h": 4
        }
      ]
    },
    ":": {
      "width": 6,
      "on": [],
      "rects": [
        {
          "x": 1,
          "y": 6,
          "w": 4,
          "h": 4
        },
        {
          "x": 1,
          "y": 16,
          "w": 4,
          "h": 4
        }
      ]
    },
    " ": {
      "width": 8,
      "on": [],
      "rects": []
    }
  }
} as const;

export type SegmentName = keyof typeof heroFont.segments;
