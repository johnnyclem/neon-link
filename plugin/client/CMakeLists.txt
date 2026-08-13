# Portable NEON LINK HTTP client. C++17, no JUCE, no ESP-IDF.
# Wired into the host test graph from host/CMakeLists.txt.

get_filename_component(NEON_CJSON_DIR
  ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/cjson ABSOLUTE)

add_library(neon_client STATIC
  src/api.cpp
  src/bind.cpp
  src/backoff.cpp
  src/http.cpp
  src/patch.cpp
  src/ratelimit.cpp
  src/status.cpp
)
target_include_directories(neon_client
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${NEON_CJSON_DIR})
target_compile_features(neon_client PUBLIC cxx_std_17)
target_link_libraries(neon_client PUBLIC neon_core neon_hal)
