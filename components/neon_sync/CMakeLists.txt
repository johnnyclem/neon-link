# Neon Sync: the Link-free session protocol core (docs/NEON_SYNC.md).
# Dual-mode like neon_core: an ESP-IDF component in firmware builds, a
# plain static library for host-native tests. Sources are portable C++17
# only — sans-I/O, no sockets, no ESP-IDF includes — so the whole protocol
# (discovery, agreement, clock discipline) runs under the host simulator
# with sanitizers on. The ESP glue lives in components/neon_sync_esp.

set(NEON_SYNC_SRCS
  src/wire.cpp
  src/peer_clock.cpp
  src/node.cpp
  src/daw_follower.cpp
)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${NEON_SYNC_SRCS} INCLUDE_DIRS include
                         REQUIRES neon_hal neon_core)
  target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
else()
  add_library(neon_sync STATIC ${NEON_SYNC_SRCS})
  target_include_directories(neon_sync PUBLIC include)
  target_compile_features(neon_sync PUBLIC cxx_std_17)
  target_link_libraries(neon_sync PUBLIC neon_hal neon_core)
endif()
