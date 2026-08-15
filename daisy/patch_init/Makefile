# NEON LINK — Daisy patch.init() headless build (no screen;
# docs/DAISY.md §8). A Eurorack module out of the box: GATE OUT 1/2 are
# CLK1/RESET, GATE IN 1/2 are CLK/RST IN, CV OUT 2 is Tempo CV (a real 0-5 V),
# the button is quantized start/stop, the toggle picks the clock source,
# and knob 1 is a soft-pickup tempo control. Audio outs can carry
# pulse-as-audio roles for more clock outputs (include/board_pins_patch_init.h).
#
#   make -C ../../third_party/libDaisy -j   # once: build libdaisy.a
#   make -j                                 # build build/neon-link-patch-init.{elf,bin}
#   make program-dfu                        # flash over USB DFU

BOARD_NAME = patch-init
BOARD_DEF = NEON_BOARD_PATCH_INIT
BOARD_SRCS = main_headless.cpp controls_patch_init.cpp buttons_daisy.cpp

DAISY_DIR = ..
REPO_ROOT = ../..

include ../boards.mk
