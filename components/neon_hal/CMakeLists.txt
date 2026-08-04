# Header-only hardware abstraction interfaces, portable C++17.
# Dual-mode like neon_core: IDF component or host interface library.

if(ESP_PLATFORM)
  idf_component_register(INCLUDE_DIRS include)
else()
  add_library(neon_hal INTERFACE)
  target_include_directories(neon_hal INTERFACE include)
endif()
