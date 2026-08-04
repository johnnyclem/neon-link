if(CONFIG_NEON_LINK_STUB)
  set(ABLINK_SRCS src/link_stub.cpp)
else()
  set(ABLINK_SRCS src/link_session_esp.cpp)
endif()

idf_component_register(
  SRCS ${ABLINK_SRCS}
  INCLUDE_DIRS include
  REQUIRES neon_hal
  PRIV_REQUIRES esp_timer lwip
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

if(NOT CONFIG_NEON_LINK_STUB)
  get_filename_component(NEON_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
  set(LINK_ROOT ${NEON_REPO_ROOT}/third_party/link)
  if(NOT EXISTS ${LINK_ROOT}/include/ableton/Link.hpp)
    message(FATAL_ERROR
      "Ableton Link submodule missing — run: git submodule update --init --recursive")
  endif()
  # Link is header-only; its ESP32 platform is selected automatically via
  # the globally-defined ESP_PLATFORM macro. asio comes from Link's own
  # submodule. Exceptions are required (see sdkconfig.defaults).
  target_include_directories(${COMPONENT_LIB} PRIVATE
    ${LINK_ROOT}/include
    ${LINK_ROOT}/modules/asio-standalone/asio/include)
  target_compile_options(${COMPONENT_LIB} PRIVATE -fexceptions)
  # Keep Link's asio service task off the real-time core.
  target_compile_definitions(${COMPONENT_LIB} PRIVATE LINK_ESP_TASK_CORE_ID=0)
endif()
