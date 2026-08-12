idf_component_register(
  SRCS src/web_ui.cpp
  INCLUDE_DIRS include
  # The pre-gzipped bundle built by web/ (npm run build). It is committed so
  # this build never needs a Node toolchain; CI rebuilds it and fails if the
  # committed copy has drifted from the sources.
  EMBED_FILES www/dist/index.html.gz
  REQUIRES neon_core app_state
  PRIV_REQUIRES esp_http_server net_manager esp_timer neon_hal_esp freertos
                app_update esp_app_format
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
