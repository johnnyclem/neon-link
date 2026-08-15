# Shared build fragment for the NEON LINK Daisy-family targets
# (docs/DAISY.md §5 and §8). Each board's Makefile sets:
#
#   BOARD_NAME  short name (also the TARGET suffix / build artifact name)
#   BOARD_DEF   the NEON_BOARD_* define board_pins_daisy.h dispatches on
#   BOARD_SRCS  board-specific sources from daisy/src (main + controls)
#   DAISY_DIR   path to daisy/ from the board Makefile's directory
#   REPO_ROOT   path to the repo root from the same directory
#
# and then includes this file. Each board builds in its own directory
# (daisy/, daisy/pod/, daisy/patch_init/), so the core Makefile's
# relative `build/` output keeps the three configurations from ever
# sharing object files (they compile with different -D defines).

TARGET = neon-link-$(BOARD_NAME)

LIBDAISY_DIR = $(REPO_ROOT)/third_party/libDaisy
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

# neon_core is C++17 (same bar as the IDF, PlatformIO, and host builds).
# No Ableton Link on these targets (no network interface), so the stock
# -fno-exceptions/-fno-rtti flags stand — none of the Teensy target's
# exception-scoping or stdshim machinery is needed here.
CPP_STANDARD = -std=gnu++17

# -Os over the default -O2: the internal-flash (BOOT_NONE) image must fit
# the STM32H750's single 128 KB sector, and the pulse path is scheduled,
# not compute-bound.
OPT = -Os

CORE_SRC = $(REPO_ROOT)/components/neon_core/src

# The portable firmware core compiles in place — same sources as the
# ESP32-S3, Teensy 4.1, and host-test builds, no copies. config_json.cpp
# (and its cJSON dependency) is the one core source left out: it exists
# for the web editor's REST surface, and these targets have no network
# to serve it on (docs/DAISY.md §1).
SHARED_SRCS = \
app_state_daisy.cpp \
audio_daisy.cpp \
board_daisy.cpp \
clkin_daisy.cpp \
config_store_daisy.cpp \
link_service_daisy.cpp \
link_session_daisy.cpp \
midi_daisy.cpp \
pulse_hw_daisy.cpp \
timebase_daisy.cpp

CPP_SOURCES = \
$(addprefix $(DAISY_DIR)/src/,$(SHARED_SRCS) $(BOARD_SRCS)) \
$(filter-out $(CORE_SRC)/config_json.cpp, $(wildcard $(CORE_SRC)/*.cpp)) \
$(wildcard $(CORE_SRC)/audio/*.cpp)

C_DEFS = -D$(BOARD_DEF)

C_INCLUDES = \
-I$(DAISY_DIR)/include \
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

# Convenience: rebuild the libdaisy.a these apps link against.
.PHONY: libdaisy
libdaisy:
	$(MAKE) -C $(LIBDAISY_DIR) -j
