# Host-native build: compiles the portable neon_core library with a desktop
# toolchain and runs the unit test suite. This is the local verification
# loop (no ESP-IDF required):
#
#   cmake -S host -B build-host -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build-host -j
#   ctest --test-dir build-host --output-on-failure

cmake_minimum_required(VERSION 3.16)
project(neon_host LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(NEON_SANITIZERS "Build with ASan + UBSan" ON)

add_compile_options(-Wall -Wextra -Werror)
if(NEON_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../components/neon_hal neon_hal)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../components/neon_core neon_core)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../components/neon_sync neon_sync)

find_package(Threads REQUIRED)

add_executable(neon_tests
  tests/doctest_main.cpp
  tests/test_pulse_channel.cpp
  tests/test_multi_engine.cpp
  tests/test_config.cpp
  tests/test_fixed_math.cpp
  tests/test_timeline.cpp
  tests/test_link_snapshot.cpp
  tests/test_net_preference.cpp
  tests/test_ext_clock.cpp
  tests/test_ui.cpp
  tests/test_midi.cpp
  tests/test_midi_clock.cpp
  tests/test_midi_serial.cpp
  tests/test_status_led.cpp
  tests/test_epd_canvas.cpp
  tests/test_epd_front.cpp
  tests/test_epd_refresh.cpp
  tests/test_config_json.cpp
  tests/test_rhythm.cpp
  tests/test_transport.cpp
  tests/test_sample_clock.cpp
  tests/test_audio_click.cpp
  tests/test_pulse_audio.cpp
  tests/test_audio_mixer.cpp
  tests/test_frame_ring.cpp
  tests/test_resampler.cpp
  tests/test_audio_lpf.cpp
  tests/test_jitter_buffer.cpp
  tests/test_synth_voice.cpp
  tests/test_quadrature.cpp
  tests/test_telemetry_csv.cpp
  tests/test_telemetry_emitter.cpp
  tests/test_nsync_wire.cpp
  tests/test_nsync_clock.cpp
  tests/test_nsync_node.cpp
  tests/test_nsync_sim.cpp
)
target_include_directories(neon_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/doctest
  ${CMAKE_CURRENT_SOURCE_DIR}/fakes)
target_link_libraries(neon_tests PRIVATE neon_core neon_hal neon_sync
                                         Threads::Threads)

enable_testing()
add_test(NAME neon_tests COMMAND neon_tests)

# Debug aid: prints the edge timeline for a tempo/PPQN/duration.
add_executable(neon_sim sim/neon_sim.cpp)
target_link_libraries(neon_sim PRIVATE neon_core)

# Renders every device screen through the firmware's own render_ui() and
# writes design/screens.json, which the style guide's simulator displays.
add_executable(neon_screens sim/neon_screens.cpp)
target_link_libraries(neon_screens PRIVATE neon_core)

# Portable plugin HTTP client. No JUCE. Same sanitizers as neon_tests.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../plugin/client/CMakeLists.txt")
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../plugin/client neon_client)
  add_executable(neon_client_tests
    tests/doctest_main.cpp
    ../plugin/client/tests/test_bind.cpp
    ../plugin/client/tests/test_status.cpp
    ../plugin/client/tests/test_patch.cpp
    ../plugin/client/tests/test_http.cpp
    ../plugin/client/tests/test_client.cpp
    ../plugin/client/tests/test_backoff.cpp
    ../plugin/client/tests/test_mic.cpp
  )
  target_include_directories(neon_client_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/doctest)
  target_compile_definitions(neon_client_tests PRIVATE
    NEON_CLIENT_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../plugin/client/tests/fixtures")
  target_link_libraries(neon_client_tests PRIVATE neon_client Threads::Threads)
  add_test(NAME neon_client_tests COMMAND neon_client_tests)
endif()
