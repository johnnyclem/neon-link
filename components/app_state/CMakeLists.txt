idf_component_register(
  SRCS src/config_store.cpp src/timeline_bus.cpp src/audio_bus.cpp
  INCLUDE_DIRS include
  REQUIRES neon_core neon_hal_esp
  PRIV_REQUIRES esp_timer
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
