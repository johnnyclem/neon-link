idf_component_register(
  SRCS src/oled_ui.cpp src/panel128.cpp
  INCLUDE_DIRS include
  REQUIRES neon_core app_state
  # app_update/esp_app_format: esp_app_desc.h / esp_app_get_description(),
  # for the System screen's VERSION row (G5) — same pair web_ui.cpp and
  # net_manager.cpp already require for the same header.
  PRIV_REQUIRES neon_hal_esp neon_board net_manager driver esp_timer
                app_update esp_app_format
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
