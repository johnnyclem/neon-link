# ESP glue for Neon Sync (docs/NEON_SYNC.md): a socket task that drives the
# portable nsync::Node from components/neon_sync. Registered unconditionally
# (the linker drops it when nothing references nsyncesp::session(), i.e.
# whenever CONFIG_NEON_SYNC is off).

idf_component_register(
  SRCS src/session_esp.cpp
  INCLUDE_DIRS include
  REQUIRES neon_hal neon_sync
  PRIV_REQUIRES esp_timer esp_wifi lwip esp_netif esp_hw_support
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
