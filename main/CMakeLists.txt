idf_component_register(
  SRCS app_main.cpp tasks_core0.cpp tasks_core1.cpp
       wifi.cpp link_service.cpp midi_service.cpp audio_service.cpp
  INCLUDE_DIRS .
  REQUIRES neon_core neon_hal neon_hal_esp ableton_link net_manager
           neon_board app_state oled_ui ble_midi web_ui amy_synth
           nvs_flash esp_timer esp_wifi esp_netif esp_event driver
           esp_hw_support app_update esp_psram
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
