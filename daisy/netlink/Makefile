# NEON LINK — Daisy Seed "netlink" build (docs/DAISY.md §9): the Seed
# flagship (OLED + encoders) plus USB gadget networking. The Seed
# enumerates as a USB Ethernet adapter (CDC-ECM); once the host bridges
# or shares its connection, the module runs real Ableton Link, serves
# the web editor at http://<device-name>.local/, and answers the same
# REST surface the VST plugin speaks.
#
# The Link + lwIP + web bundle image outgrows the 128 KB internal-flash
# sector, so this config uses the Daisy bootloader: APP_TYPE=BOOT_SRAM
# puts the app image in QSPI at 0x90040000, and the bootloader copies it
# into the 480 KB AXI SRAM to execute. The QSPI config store keeps
# working — nothing executes from the chip it erases (boards.mk).
#
#   make -C ../../third_party/libDaisy -j   # once: build libdaisy.a
#   make -j                                 # build build/neon-link-netlink.{elf,bin}
#   make program-dfu                        # flash via the Daisy bootloader
#     (first install the bootloader itself: make program-boot)

BOARD_NAME = netlink
BOARD_DEF = NEON_BOARD_SEED
BOARD_SRCS = main.cpp oled_daisy.cpp encoders_daisy.cpp

DAISY_DIR = ..
REPO_ROOT = ../..

APP_TYPE = BOOT_SRAM

# The real ableton::Link session (netlink/src) replaces the offline
# InternalTimeline adapter behind the daisy_session() seam.
SESSION_SRC =

# The web editor's REST surface needs the JSON codec the offline
# configs exclude.
CORE_EXCLUDE =

LWIP = $(REPO_ROOT)/third_party/lwip/src

# Embed the committed web bundle at parse time (rewrites only when the
# input is newer, so incremental builds stay incremental).
$(shell python3 scripts/gen_web_bundle.py \
  $(REPO_ROOT)/components/web_ui/www/dist/index.html.gz gen/web_bundle.c)

EXTRA_CPP_SOURCES = \
src/usbnet_daisy.cpp \
src/httpd_netlink.cpp \
src/link_runtime_netlink.cpp \
src/link_session_netlink.cpp

EXTRA_C_SOURCES = \
src/usbd_ecm.c \
src/usbd_ecm_desc.c \
gen/web_bundle.c \
$(REPO_ROOT)/third_party/cjson/cJSON.c \
$(LWIP)/core/def.c \
$(LWIP)/core/inet_chksum.c \
$(LWIP)/core/init.c \
$(LWIP)/core/ip.c \
$(LWIP)/core/mem.c \
$(LWIP)/core/memp.c \
$(LWIP)/core/netif.c \
$(LWIP)/core/pbuf.c \
$(LWIP)/core/stats.c \
$(LWIP)/core/sys.c \
$(LWIP)/core/tcp.c \
$(LWIP)/core/tcp_in.c \
$(LWIP)/core/tcp_out.c \
$(LWIP)/core/timeouts.c \
$(LWIP)/core/udp.c \
$(LWIP)/core/ipv4/acd.c \
$(LWIP)/core/ipv4/autoip.c \
$(LWIP)/core/ipv4/dhcp.c \
$(LWIP)/core/ipv4/etharp.c \
$(LWIP)/core/ipv4/icmp.c \
$(LWIP)/core/ipv4/igmp.c \
$(LWIP)/core/ipv4/ip4.c \
$(LWIP)/core/ipv4/ip4_addr.c \
$(LWIP)/core/ipv4/ip4_frag.c \
$(LWIP)/netif/ethernet.c \
$(LWIP)/apps/mdns/mdns.c \
$(LWIP)/apps/mdns/mdns_domain.c \
$(LWIP)/apps/mdns/mdns_out.c

# Include order matters twice over: link_platform must precede the
# upstream Link headers (the shadowed Config.hpp / AsioTypes.hpp are the
# platform selection), and stdshim must precede the toolchain's C++
# headers (its <mutex>/<condition_variable> reach the real ones through
# include_next).
EXTRA_C_INCLUDES = \
-I$(DAISY_DIR)/src \
-Isrc \
-Iport \
-Ilink_platform \
-Istdshim \
-I$(LWIP)/include \
-I$(REPO_ROOT)/third_party/link/include \
-I$(REPO_ROOT)/third_party/cjson \
-I$(REPO_ROOT)/components/ableton_link/include

EXTRA_C_DEFS = -DNEON_DAISY_FIRMWARE='"daisy-netlink"'

# Ableton Link throws on hard faults (socket loss, bad payloads):
# exceptions on for this config only, appended after the core Makefile's
# -fno-exceptions so it wins. -Wno-multichar: Link's payload keys are
# intentional multi-character constants ('sess', 'tmln', ...).
EXTRA_CPPFLAGS_POST = -fexceptions -Wno-multichar

include ../boards.mk
