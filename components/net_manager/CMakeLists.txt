idf_component_register(
  SRCS src/net_manager.cpp
  INCLUDE_DIRS include
  REQUIRES neon_core esp_eth esp_netif esp_event
  PRIV_REQUIRES driver esp_hw_support neon_board
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
