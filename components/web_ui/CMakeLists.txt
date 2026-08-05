idf_component_register(
  SRCS src/web_ui.cpp
  INCLUDE_DIRS include
  EMBED_TXTFILES www/index.html
  REQUIRES neon_core app_state
  PRIV_REQUIRES esp_http_server net_manager esp_timer
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
