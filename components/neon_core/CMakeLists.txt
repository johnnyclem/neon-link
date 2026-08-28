# Dual-mode component: registers as an ESP-IDF component in firmware builds
# and as a plain static library for host-native unit tests. Sources here are
# portable C++17 only — no ESP-IDF includes allowed (enforced by the host CI
# job, which compiles this library with gcc + -Werror + sanitizers). The one
# vendored C dependency is cJSON (third_party/cjson), compiled privately.

get_filename_component(NEON_CJSON_DIR
  ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/cjson ABSOLUTE)

set(NEON_CORE_SRCS
  ${NEON_CJSON_DIR}/cJSON.c
  src/config_json.cpp
  src/pulse_channel.cpp
  src/multi_engine.cpp
  src/link_snapshot.cpp
  src/config_model.cpp
  src/ext_clock.cpp
  src/gfx_framebuffer.cpp
  src/epd_canvas.cpp
  src/menu_model.cpp
  src/epd_front.cpp
  src/ui_widgets.cpp
  src/ui_render.cpp
  src/ble_midi_parser.cpp
  src/serial_midi_parser.cpp
  src/midi_encoder.cpp
  src/midi_clock_engine.cpp
  src/midi_clock_pll.cpp
  src/midi_ble_time_mapper.cpp
  src/midi_sync_follower.cpp
  src/midi_router.cpp
  src/status_led.cpp
  src/transport.cpp
  src/audio/sample_clock.cpp
  src/audio/click.cpp
  src/audio/pulse_render.cpp
  src/audio/mixer.cpp
  src/audio/resampler.cpp
  src/audio/jitter_buffer.cpp
  src/audio/lpf.cpp
  src/audio/synth_voice.cpp
  src/telemetry/csv.cpp
  src/telemetry/linksync_csv.cpp
  src/telemetry/midi_pll_csv.cpp
  src/telemetry/emitter.cpp
)

if(ESP_PLATFORM)
  idf_component_register(SRCS ${NEON_CORE_SRCS} INCLUDE_DIRS include
                         REQUIRES neon_hal)
  target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
  target_include_directories(${COMPONENT_LIB} PRIVATE ${NEON_CJSON_DIR})
else()
  add_library(neon_core STATIC ${NEON_CORE_SRCS})
  target_include_directories(neon_core PUBLIC include)
  target_include_directories(neon_core PRIVATE ${NEON_CJSON_DIR})
  target_compile_features(neon_core PUBLIC cxx_std_17)
  target_link_libraries(neon_core PUBLIC neon_hal)
endif()

# Vendored C code is not held to the project's warning bar.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
  set_source_files_properties(${NEON_CJSON_DIR}/cJSON.c
                              PROPERTIES COMPILE_OPTIONS "-w")
endif()
