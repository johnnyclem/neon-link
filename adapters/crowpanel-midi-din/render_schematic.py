#!/usr/bin/env python3
"""Render schematic.png — CrowPanel 2×DIN + 6N138 MIDI dongle."""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

W, H = 1700, 1100
BG = (252, 250, 245)
INK = (28, 28, 28)
RED = (176, 36, 36)
MUTED = (108, 108, 108)
CREAM = (248, 244, 236)
CHIP = (236, 236, 228)
JACK = (244, 244, 238)

im = Image.new("RGB", (W, H), BG)
d = ImageDraw.Draw(im)


def font(size, bold=False):
    path = ("/System/Library/Fonts/Supplemental/Arial Bold.ttf" if bold
            else "/System/Library/Fonts/Supplemental/Arial.ttf")
    try:
        return ImageFont.truetype(path, size)
    except OSError:
        return ImageFont.load_default()


FT = font(30, True)
F = font(17)
FS = font(14)
FB = font(17, True)


def t(xy, s, f=F, fill=INK, anchor="lt"):
    d.text(xy, s, font=f, fill=fill, anchor=anchor)


def L(a, b, w=2, fill=INK):
    d.line([a, b], fill=fill, width=w)


def H(x1, x2, y, w=2):
    L((x1, y), (x2, y), w)


def V(x, y1, y2, w=2):
    L((x, y1), (x, y2), w)


def dot(xy, r=4):
    x, y = xy
    d.ellipse((x - r, y - r, x + r, y + r), fill=INK)


def gnd(x, y):
    V(x, y, y + 8)
    H(x - 14, x + 14, y + 8, 2)
    H(x - 9, x + 9, y + 14, 2)
    H(x - 4, x + 4, y + 20, 2)


def flag(x, y, label, fill=RED):
    d.polygon([(x, y), (x - 9, y + 16), (x + 9, y + 16)], outline=INK, fill=BG)
    t((x, y - 2), label, FB, fill, "mb")


def res_h(x, y, label):
    d.rectangle((x - 28, y - 10, x + 28, y + 10), outline=INK, width=2, fill=BG)
    H(x - 44, x - 28, y)
    H(x + 28, x + 44, y)
    t((x, y - 16), label, FS, MUTED, "mb")
    return x - 44, x + 44


def res_v(x, y, label):
    d.rectangle((x - 10, y - 28, x + 10, y + 28), outline=INK, width=2, fill=BG)
    V(x, y - 44, y - 28)
    V(x, y + 28, y + 44)
    t((x + 16, y), label, FS, MUTED, "lm")
    return y - 44, y + 44


def cap_v(x, y, label):
    H(x - 14, x + 14, y - 6, 3)
    H(x - 14, x + 14, y + 6, 3)
    V(x, y - 22, y - 6)
    V(x, y + 6, y + 22)
    t((x + 18, y), label, FS, MUTED, "lm")


def diode_v(x, y_top, y_bot, label):
    """Cathode at y_top (bar), anode at y_bot (arrow points up = reverse across LED)."""
    mid = (y_top + y_bot) // 2
    V(x, y_top, mid - 12)
    H(x - 12, x + 12, mid - 12, 3)  # cathode bar
    d.polygon([(x, mid + 14), (x - 12, mid - 10), (x + 12, mid - 10)], outline=INK, fill=BG)
    V(x, mid + 14, y_bot)
    t((x - 16, mid), label, FS, MUTED, "rm")


def pin_circle(x, y, n):
    d.ellipse((x - 11, y - 11, x + 11, y + 11), outline=INK, width=2, fill=BG)
    t((x, y), str(n), FS, INK, "mm")


def horseshoe(cx, cy, r=78):
    d.arc((cx - r, cy - r - 8, cx + r, cy + r - 8), 200, 340, fill=INK, width=3)
    pts = {
        1: (cx - 42, cy - 52),
        3: (cx + 42, cy - 52),
        4: (cx - 64, cy + 6),
        2: (cx, cy + 62),
        5: (cx + 64, cy + 6),
    }
    for n, p in pts.items():
        pin_circle(*p, n)
    t((cx, cy + r + 18), "into jack, notch down", FS, MUTED, "mt")
    return pts


# Title
t((40, 24), 'CrowPanel 5.79"   MIDI IN / OUT', FT)
t((40, 60), "two 5-pin DIN 180° jacks  ·  6N138  ·  UART1 31250 baud  ·  TX IO21  RX IO38", F, MUTED)
H(40, 1660, 82, 1)

# ========== TOP: CrowPanel + OUT ==========
d.rounded_rectangle((40, 100, 360, 430), 8, fill=CREAM, outline=INK, width=2)
t((200, 118), "CROWPANEL IDC", FB, anchor="mt")
t((200, 142), "beep from outside the case", FS, MUTED, "mt")

hdr = [
    (190, "5 V", "USB VBUS / pad P2  →  6N138 pin 8 only", RED),
    (238, "3V3", "OUT current source + RX pull-up", INK),
    (286, "GND", "common with USB 5 V return", INK),
    (334, "TX  IO21", "UART1 MIDI OUT", INK),
    (382, "RX  IO38", "UART1 MIDI IN  (after opto)", INK),
]
# header is 100-430, 5 rows starting 180
hy = [180, 228, 276, 324, 372]
hlabels = [
    ("5 V", "USB VBUS / pad P2  →  U1 pin 8 only", RED),
    ("3V3", "OUT loop + RX pull-up", INK),
    ("GND", "common, including 5 V return", INK),
    ("TX   IO21", "UART1 MIDI OUT", INK),
    ("RX   IO38", "UART1 MIDI IN after opto", INK),
]
for y, (name, note, col) in zip(hy, hlabels):
    d.ellipse((62, y - 8, 78, y + 8), outline=INK, width=2, fill=BG)
    t((92, y), name, FB, col, "lm")
    t((92, y + 16), note, FS, MUTED, "lm")

# J1
d.rounded_rectangle((980, 100, 1360, 430), 8, fill=JACK, outline=INK, width=2)
t((1170, 118), "J1   MIDI OUT", FB, RED, "mt")
t((1170, 142), "5-pin DIN 180° female", FS, MUTED, "mt")
p1 = horseshoe(1170, 280)

# OUT nets
# 3V3 (hy[1]=228) -- R1 -- J1 pin 4
H(360, 500, 228)
res_h(560, 228, "R1  220 Ω")
H(604, 980, 228)
H(980, 1040, 228)
V(1040, 228, p1[4][1])
H(1040, p1[4][0] - 11, p1[4][1])
dot((1040, 228))
t((720, 206), "current source", FS, MUTED, "mb")

# TX (hy[3]=324) -- R2 -- J1 pin 5
H(360, 500, 324)
res_h(560, 324, "R2  220 Ω")
H(604, 980, 324)
H(980, 1300, 324)
V(1300, 324, p1[5][1])
H(p1[5][0] + 11, 1300, p1[5][1])
dot((1300, 324))
t((720, 302), "idle high · start bit sinks current", FS, MUTED, "mb")

# GND (hy[2]=276) -- J1 pin 2
H(360, 1170, 276)
V(1170, 276, p1[2][1] - 11)
dot((1170, 276))
t((720, 258), "shield", FS, MUTED, "mb")

t((1170, 408), "pins 1 and 3  NC", FS, MUTED, "mt")

# ========== BOTTOM: 6N138 + IN ==========
d.rounded_rectangle((40, 460, 820, 1000), 8, fill=CHIP, outline=INK, width=2)
t((430, 478), "U1   6N138", FB, RED, "mt")
t((430, 502), "DIP-8   notch up   ·   pin 7 must not float", FS, MUTED, "mt")

# DIP body
d.rounded_rectangle((160, 560, 520, 900), 4, fill=(250, 250, 246), outline=INK, width=2)
d.arc((310, 552, 370, 590), 0, 180, fill=INK, width=2)

# pin Y positions
yp = {1: 600, 2: 670, 3: 740, 4: 810, 8: 600, 7: 670, 6: 740, 5: 810}
ln = {1: "1  NC", 2: "2  A", 3: "3  K", 4: "4  NC"}
rn = {8: "VCC  8", 7: "BASE  7", 6: "OUT  6", 5: "GND  5"}
for n in (1, 2, 3, 4):
    y = yp[n]
    H(140, 160, y)
    d.ellipse((128, y - 7, 142, y + 7), outline=INK, width=2, fill=BG)
    t((176, y), ln[n], F, INK, "lm")
for n in (8, 7, 6, 5):
    y = yp[n]
    H(520, 540, y)
    d.ellipse((538, y - 7, 552, y + 7), outline=INK, width=2, fill=BG)
    t((504, y), rn[n], F, INK, "rm")

# internal LED 2 -> 3
d.polygon([(250, 678), (250, 702), (278, 690)], outline=INK, fill=BG)
V(280, 678, 702, 2)
t((265, 658), "LED", FS, MUTED, "mb")
t((400, 790), "open collector", FS, MUTED, "mm")

# D1 reverse across pins 2-3, left of the DIP
diode_v(100, yp[2], yp[3], "D1 1N4148")
H(100, 140, yp[2])
H(100, 140, yp[3])
dot((140, yp[2]))
dot((140, yp[3]))

# pin 8 = 5V + C1
flag(640, 568, "5 V")
V(640, 584, yp[8])
H(540, 640, yp[8])
dot((640, yp[8]))
cap_v(640, 700, "C1  100 nF")
V(640, yp[8], 678)
V(640, 722, 930)
gnd(640, 930)

# pin 7 = R4 to GND
H(540, 720, yp[7])
res_h(780, yp[7], "R4  10 kΩ")
H(824, 860, yp[7])
V(860, yp[7], 930)
gnd(860, 930)
dot((860, yp[7]))

# pin 6 = RX + R5 to 3V3
H(540, 640, yp[6])
dot((600, yp[6]))
d.rectangle((670 - 28, yp[6] - 10, 670 + 28, yp[6] + 10), outline=INK, width=2, fill=BG)
H(698, 740, yp[6])
flag(740, yp[6] - 36, "3V3")
V(740, yp[6] - 20, yp[6])
dot((740, yp[6]))
t((670, yp[6] - 16), "R5  10 kΩ", FS, MUTED, "mb")

# RX from pin 6 into the gutter, up to header RX. x=850 — not 930 (J2 pin 4).
V(600, yp[6], 880)
H(600, 850, 880)
V(850, 372, 880)
H(360, 850, 372)
dot((600, yp[6]))
dot((850, 372))
dot((850, 880))
t((720, 862), "to RX  IO38", FS, RED, "mb")

# pin 5 GND of the chip
H(540, 580, yp[5])
V(580, yp[5], 930)
gnd(580, 930)
dot((580, yp[5]))

# J2 box
d.rounded_rectangle((980, 460, 1660, 1000), 8, fill=JACK, outline=INK, width=2)
t((1320, 478), "J2   MIDI IN", FB, RED, "mt")
t((1320, 502), "5-pin DIN 180° female   ·   opto side", FS, MUTED, "mt")
p2 = horseshoe(1320, 700, r=90)
t((1320, 848), "pins 1 and 3  NC", FS, MUTED, "mt")

# J2 pin 4 -- R3 -- U1 pin 2 (anode). Over the DIP, not through it.
H(p2[4][0] - 11, 930, p2[4][1])
V(930, 530, p2[4][1])
H(100, 930, 530)
res_h(700, 530, "R3  220 Ω")
V(100, 530, yp[2])
H(100, 140, yp[2])
dot((930, p2[4][1]))
dot((100, yp[2]))
dot((100, 530))

# J2 pin 5 -- U1 pin 3 (cathode). Not ground. Across the gap above both boxes.
H(p2[5][0] + 11, 1640, p2[5][1])
V(1640, 442, p2[5][1])
H(70, 1640, 442)
V(70, 442, yp[3])
H(70, 140, yp[3])
dot((1640, p2[5][1]))
dot((70, yp[3]))
dot((70, 442))
t((860, 428), "pin 5 → cathode  (current sink, not GND)", FS, MUTED, "mb")

# J2 pin 2 shield to GND — local, do not share with pin 5
V(p2[2][0], p2[2][1] + 11, 960)
gnd(p2[2][0], 960)
t((p2[2][0] + 18, 972), "shield only", FS, MUTED, "lm")

# Footer
t((40, 1030), "Idle: no LED current → transistor off → pin 6 pulled to 3V3 → UART idle.  Start bit: LED on → pin 6 low.", FS, MUTED)
t((40, 1054), "Do not join 5 V to CrowPanel 3V3.  3V3-only fallback: U1 pin 8 → 3V3 and change R5 to 1 kΩ.  Pin 7 (R4) is required.", FS, MUTED)
t((1660, 1042), "adapters/crowpanel-midi-din", FS, MUTED, "rm")

im.save(Path(__file__).with_name("schematic.png"), "PNG")
print("wrote schematic.png")
