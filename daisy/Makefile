# NEON LINK — Daisy Seed build (the OLED flagship of the Daisy family;
# docs/DAISY.md). Headless siblings live in pod/ and patch_init/.
#
#   make -C third_party/libDaisy -j     # once: build libdaisy.a
#   make -j                             # build build/neon-link-daisy.{elf,bin}
#   make program-dfu                    # flash over USB DFU (hold BOOT, tap RESET)

BOARD_NAME = daisy
BOARD_DEF = NEON_BOARD_SEED
BOARD_SRCS = main.cpp oled_daisy.cpp encoders_daisy.cpp

DAISY_DIR = .
REPO_ROOT = ..

include boards.mk
