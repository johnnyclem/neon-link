idf_component_register(
  SRCS src/pulse_hw_gptimer.cpp src/storage_nvs.cpp src/tempo_cv_ledc.cpp
       src/clkin_capture.cpp
  INCLUDE_DIRS include
  REQUIRES neon_hal driver esp_timer nvs_flash
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
