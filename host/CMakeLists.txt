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
  tests/test_config_json.cpp
)
target_include_directories(neon_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/doctest
  ${CMAKE_CURRENT_SOURCE_DIR}/fakes)
target_link_libraries(neon_tests PRIVATE neon_core neon_hal Threads::Threads)

enable_testing()
add_test(NAME neon_tests COMMAND neon_tests)

# Debug aid: prints the edge timeline for a tempo/PPQN/duration.
add_executable(neon_sim sim/neon_sim.cpp)
target_link_libraries(neon_sim PRIVATE neon_core)
