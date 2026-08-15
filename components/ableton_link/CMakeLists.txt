get_filename_component(NEON_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
set(LINK_ROOT ${NEON_REPO_ROOT}/third_party/link)

if(CONFIG_NEON_LINK_STUB)
  set(ABLINK_SRCS src/link_stub.cpp src/atomic_shim.c src/priority.cpp)
else()
  # asio is built in separate-compilation mode (like Espressif's asio port):
  # asio.cpp provides the non-inline implementation, except posix_event's
  # constructor, which src/asio_compat.cpp replaces (see the guard define).
  set(ABLINK_SRCS
    src/link_session_esp.cpp
    src/asio_compat.cpp
    src/atomic_shim.c
    src/priority.cpp
    ${LINK_ROOT}/modules/asio-standalone/asio/src/asio.cpp)
  if(CONFIG_NEON_LINK_AUDIO)
    list(APPEND ABLINK_SRCS src/link_audio_esp.cpp)
  endif()
  # lwIP's sys/socket.h never declares socketpair(); sock_utils implements
  # it and ships the declaration in socketpair.h. asio's socket_ops.ipp
  # calls ::socketpair, so force-include the declaration for the asio TU.
  # (Guarded: IDF evaluates component CMakeLists in script mode during
  # early expansion, where set_source_files_properties is not allowed.)
  if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set_source_files_properties(
      ${LINK_ROOT}/modules/asio-standalone/asio/src/asio.cpp
      PROPERTIES COMPILE_OPTIONS "-include;socketpair.h")
  endif()
endif()

# The no-op ILinkAudio stands in whenever there is no ableton::LinkAudio
# to call: the stub leg, and any build whose Link submodule predates 4.0.
if(NOT CONFIG_NEON_LINK_AUDIO)
  list(APPEND ABLINK_SRCS src/link_audio_stub.cpp)
endif()

idf_component_register(
  SRCS ${ABLINK_SRCS}
  INCLUDE_DIRS include
  REQUIRES neon_hal neon_core
  PRIV_REQUIRES esp_timer lwip esp_netif pthread esp_psram
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

if(NOT CONFIG_NEON_LINK_STUB)
  if(CONFIG_NEON_LINK_AUDIO AND NOT EXISTS ${LINK_ROOT}/include/ableton/LinkAudio.hpp)
    message(FATAL_ERROR
      "NEON_LINK_AUDIO needs Ableton Link 4.0 — check the submodule out at "
      "tag Link-4.0 (git -C third_party/link checkout Link-4.0 && "
      "git submodule update --init --recursive)")
  endif()
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
  # GCC 13 + -O2 (IDF's PERF) false-positives on AudioBuffer's implicit
  # move: it thinks the large std::array `bytes` is used uninitialized.
  # IDF promotes that warning to -Werror=uninitialized; upstream Link
  # does not, so this is ours to swallow. Keep it on this component —
  # do not silence the warning project-wide.
  target_compile_options(${COMPONENT_LIB} PRIVATE
    -fexceptions
    -Wno-error=uninitialized
    -Wno-error=maybe-uninitialized)
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
