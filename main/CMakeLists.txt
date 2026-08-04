idf_component_register(
  SRCS app_main.cpp tasks_core0.cpp tasks_core1.cpp
  INCLUDE_DIRS .
  REQUIRES neon_core neon_hal neon_hal_esp nvs_flash esp_timer
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
