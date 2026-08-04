get_filename_component(NEON_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
set(LINK_ROOT ${NEON_REPO_ROOT}/third_party/link)

if(CONFIG_NEON_LINK_STUB)
  set(ABLINK_SRCS src/link_stub.cpp)
else()
  # asio is built in separate-compilation mode (like Espressif's asio port):
  # asio.cpp provides the non-inline implementation, except posix_event's
  # constructor, which src/asio_compat.cpp replaces (see the guard define).
  set(ABLINK_SRCS
    src/link_session_esp.cpp
    src/asio_compat.cpp
    ${LINK_ROOT}/modules/asio-standalone/asio/src/asio.cpp)
endif()

idf_component_register(
  SRCS ${ABLINK_SRCS}
  INCLUDE_DIRS include
  REQUIRES neon_hal
  PRIV_REQUIRES esp_timer lwip esp_netif pthread
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

if(NOT CONFIG_NEON_LINK_STUB)
  if(NOT EXISTS ${LINK_ROOT}/include/ableton/Link.hpp)
    message(FATAL_ERROR
      "Ableton Link submodule missing — run: git submodule update --init --recursive")
  endif()
  # Link's ESP32 platform is selected automatically via the globally-defined
  # ESP_PLATFORM macro. asio comes from Link's own submodule (1.36.0).
  # link_overrides/ shadows upstream headers that predate asio 1.36 (see the
  # note inside each override) — it must come first on the include path.
  target_include_directories(${COMPONENT_LIB} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/link_overrides
    ${LINK_ROOT}/include
    ${LINK_ROOT}/modules/asio-standalone/asio/include)
  target_compile_options(${COMPONENT_LIB} PRIVATE -fexceptions)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
    # Keep Link's asio service task off the real-time core.
    LINK_ESP_TASK_CORE_ID=0
    # asio-on-ESP-IDF configuration, mirroring Espressif's official asio
    # port (esp-protocols): pthread-based threading primitives, no serial
    # ports, no typeid (IDF builds with -fno-rtti), and signal.h macros
    # newlib does not define but asio's signal_set code references.
    ASIO_STANDALONE
    ASIO_SEPARATE_COMPILATION
    ASIO_HAS_PTHREADS
    ASIO_DISABLE_SERIAL_PORT
    ASIO_NO_TYPEID
    SA_RESTART=0x01
    SA_NOCLDSTOP=0x2
    SA_NOCLDWAIT=0x4
    # Suppress asio's posix_event constructor; src/asio_compat.cpp
    # provides one that skips pthread_condattr_setclock (ENOSYS on IDF).
    ASIO_DETAIL_IMPL_POSIX_EVENT_IPP)
endif()
