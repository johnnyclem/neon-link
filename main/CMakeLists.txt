idf_component_register(
  SRCS app_main.cpp tasks_core0.cpp tasks_core1.cpp
       wifi.cpp link_service.cpp timeline_bus.cpp config_store.cpp
  INCLUDE_DIRS .
  REQUIRES neon_core neon_hal neon_hal_esp ableton_link net_manager
           neon_board nvs_flash esp_timer esp_wifi esp_netif esp_event
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
