# ESP glue for Neon Sync (docs/NEON_SYNC.md): a socket task that drives the
# portable nsync::Node from components/neon_sync.
#
# session_esp.cpp owns a static Node (≈28 KB BSS). Compile it only when
# CONFIG_NEON_SYNC is on; otherwise a stub keeps the component registered
# so Ableton Link images do not pay that SRAM (the C3 cannot spare it).

if(CONFIG_NEON_SYNC)
  set(srcs src/session_esp.cpp)
else()
  set(srcs src/session_stub.cpp)
endif()

idf_component_register(
  SRCS ${srcs}
  INCLUDE_DIRS include
  REQUIRES neon_hal neon_sync
  PRIV_REQUIRES esp_timer esp_wifi lwip esp_netif esp_hw_support
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
