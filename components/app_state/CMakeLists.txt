idf_component_register(
  SRCS src/config_store.cpp src/timeline_bus.cpp src/audio_bus.cpp
  INCLUDE_DIRS include
  REQUIRES neon_core neon_hal_esp neon_board
  # esp_hw_support: esp_mac.h/esp_read_mac and esp_random.h/esp_fill_random,
  # for deriving the first-boot AP password and device token (G1).
  PRIV_REQUIRES esp_timer esp_hw_support
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
