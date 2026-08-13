idf_component_register(
  SRCS src/pulse_hw_gptimer.cpp src/storage_nvs.cpp src/tempo_cv_ledc.cpp
       src/clkin_capture.cpp src/encoder_pcnt.cpp src/status_leds.cpp
       src/midi_uart.cpp src/i2c_bus.cpp src/gp8413.cpp src/ads1015.cpp
       src/i2s_audio.cpp src/gpio_expansion.cpp
  INCLUDE_DIRS include
  REQUIRES neon_hal neon_board driver esp_timer nvs_flash
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
# quadrature.hpp is header-only; avoid a neon_core component cycle.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
  target_include_directories(${COMPONENT_LIB} PRIVATE ../neon_core/include)
endif()
