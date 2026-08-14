# NEON LINK — Electrosmith Daisy Seed build target (docs/DAISY.md).
#
# Hardware: Daisy Seed (STM32H750 @ 400 MHz, 64 MB SDRAM, 8 MB QSPI
# flash, stereo codec), a 128×64 SSD1306/SSD1309 OLED on SPI1, two EC11
# rotary encoders, six pulse outputs, CLK/RST IN, TRS MIDI out on
# USART1, Tempo CV on the true DAC, and three status LEDs.
#
#   make -C third_party/libDaisy -j     # once: build libdaisy.a
#   make -j                             # build build/neon-link-daisy.{elf,bin}
#   make program-dfu                    # flash over USB DFU (hold BOOT, tap RESET)
#
# The portable firmware core (components/neon_core against the
# header-only components/neon_hal interfaces, plus the shared app_state
# headers) is compiled straight from the repo — same sources as the
# ESP32-S3, Teensy 4.1, and host-test builds, no copies, so screens,
# menu tree, clock math, and the config codec cannot drift between
# platforms. config_json.cpp (and its cJSON dependency) is the one core
# source left out: it exists for the web editor's REST surface, and this
# target has no network to serve it on (docs/DAISY.md §1).

TARGET = neon-link-daisy

REPO_ROOT = ..
LIBDAISY_DIR = $(REPO_ROOT)/third_party/libDaisy
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

# neon_core is C++17 (same bar as the IDF, PlatformIO, and host builds).
# No Ableton Link on this target (no network interface), so the stock
# -fno-exceptions/-fno-rtti flags stand — none of the Teensy target's
# exception-scoping or stdshim machinery is needed here.
CPP_STANDARD = -std=gnu++17

# -Os over the default -O2: the internal-flash (BOOT_NONE) image must fit
# the STM32H750's single 128 KB sector, and the pulse path is scheduled,
# not compute-bound.
OPT = -Os

CORE_SRC = $(REPO_ROOT)/components/neon_core/src

CPP_SOURCES = \
$(wildcard src/*.cpp) \
$(filter-out $(CORE_SRC)/config_json.cpp, $(wildcard $(CORE_SRC)/*.cpp)) \
$(wildcard $(CORE_SRC)/audio/*.cpp)

C_INCLUDES = \
-Iinclude \
-I$(REPO_ROOT)/components/neon_core/include \
-I$(REPO_ROOT)/components/neon_hal/include \
-I$(REPO_ROOT)/components/app_state/include

# Internal flash, no bootloader: code executes XIP from 0x08000000, which
# is what makes the QSPI config store safe to erase/program at runtime
# (config_store_daisy.cpp). Switching to APP_TYPE=BOOT_QSPI would put the
# code and the config store on the same chip — do not, without moving the
# store first.
APP_TYPE = BOOT_NONE

include $(SYSTEM_FILES_DIR)/Makefile

# Project sources hold the same zero-warning bar as the other targets.
# Appended after the include so it lands on top of libDaisy's CPPFLAGS.
# The libDaisy include roots are re-listed as -isystem, which makes GCC
# drop the core Makefile's -I for the same directories and treat them as
# system headers — -Wextra then applies to project and neon_core sources
# only, not to upstream headers they include (the narrow scoping rule
# from teensy41/platformio.ini's -Wno-multichar).
CPPFLAGS += -Wextra \
-isystem $(LIBDAISY_DIR) \
-isystem $(LIBDAISY_DIR)/src \
-isystem $(LIBDAISY_DIR)/src/sys \
-isystem $(LIBDAISY_DIR)/Drivers/CMSIS_5/CMSIS/Core/Include \
-isystem $(LIBDAISY_DIR)/Drivers/CMSIS-Device/ST/STM32H7xx/Include \
-isystem $(LIBDAISY_DIR)/Drivers/STM32H7xx_HAL_Driver/Inc

# The only C sources in this build are libDaisy's startup files; their
# stock -Wattributes note (FP clobber in the reset handler) is upstream's
# to keep, not ours to fix.
CFLAGS += -Wno-attributes

# Convenience: rebuild the libdaisy.a this app links against.
.PHONY: libdaisy
libdaisy:
	$(MAKE) -C $(LIBDAISY_DIR) -j
