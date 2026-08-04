idf_component_register(
  SRCS src/oled_ui.cpp
  INCLUDE_DIRS include
  REQUIRES neon_core app_state
  PRIV_REQUIRES neon_hal_esp neon_board net_manager esp_lcd driver esp_timer
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
