idf_component_register(
  SRCS src/pulse_hw_gptimer.cpp
  INCLUDE_DIRS include
  REQUIRES neon_hal driver esp_timer
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
