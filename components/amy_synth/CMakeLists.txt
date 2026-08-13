# The synth seam. AMY (third_party/amy, MIT) when CONFIG_NEON_AUDIO_AMY is
# set, neon_core's own SynthVoiceBank otherwise — same header, same
# behaviour from every caller's point of view.

get_filename_component(NEON_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
set(AMY_ROOT ${NEON_REPO_ROOT}/third_party/amy)

if(CONFIG_NEON_AUDIO_AMY)
  file(GLOB AMY_SRCS ${AMY_ROOT}/src/*.c)
  idf_component_register(
    SRCS src/amy_synth.cpp ${AMY_SRCS}
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS ${AMY_ROOT}/src
    REQUIRES neon_core
  )
  if(NOT EXISTS ${AMY_ROOT}/src/amy.h)
    message(FATAL_ERROR
      "NEON_AUDIO_AMY is set but third_party/amy is missing — run: "
      "git submodule update --init --recursive")
  endif()
  # Vendored C is not held to this project's warning bar.
  if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set_source_files_properties(${AMY_SRCS} PROPERTIES COMPILE_OPTIONS "-w")
  endif()
else()
  idf_component_register(
    SRCS src/amy_stub.cpp
    INCLUDE_DIRS include
    REQUIRES neon_core
  )
endif()

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
