# NEON LINK — Daisy Pod headless build (no screen; docs/DAISY.md §8).
# Control surface: the Pod's encoder (tempo / quantized start-stop),
# button 1 = tap tempo, button 2 = resync, RGB LEDs for beat and
# run/ext-lock. Pulse outs, CLK/RST IN, TRS MIDI out, and Tempo CV ride
# the expansion-header GPIO (include/board_pins_pod.h).
#
#   make -C ../../third_party/libDaisy -j   # once: build libdaisy.a
#   make -j                                 # build build/neon-link-pod.{elf,bin}
#   make program-dfu                        # flash over USB DFU

BOARD_NAME = pod
BOARD_DEF = NEON_BOARD_POD
BOARD_SRCS = main_headless.cpp controls_pod.cpp encoders_daisy.cpp buttons_daisy.cpp

DAISY_DIR = ..
REPO_ROOT = ../..

include ../boards.mk
