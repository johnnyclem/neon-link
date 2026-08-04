# Dual-mode component: registers as an ESP-IDF component in firmware builds
# and as a plain static library for host-native unit tests. Sources here are
# portable C++17 only — no ESP-IDF includes allowed (enforced by the host CI
# job, which compiles this library with gcc + -Werror + sanitizers).

set(NEON_CORE_SRCS
  src/pulse_channel.cpp
  src/multi_engine.cpp
  src/link_snapshot.cpp
  src/config_model.cpp
)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${NEON_CORE_SRCS} INCLUDE_DIRS include
                         REQUIRES neon_hal)
  target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
else()
  add_library(neon_core STATIC ${NEON_CORE_SRCS})
  target_include_directories(neon_core PUBLIC include)
  target_compile_features(neon_core PUBLIC cxx_std_17)
  target_link_libraries(neon_core PUBLIC neon_hal)
endif()
