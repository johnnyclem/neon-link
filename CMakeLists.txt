cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(neon_link)

# `idf.py flash` only programs ota_0. A USB flash boots PENDING_VERIFY;
# the next reset (or a crash before mark_valid) loads ota_1. If that slot
# is erased you get the blank OLED / two-pixels-lit brick. Mirror the app
# at whatever offset THIS partition table actually uses — do not hardcode
# 0x620000; the 8 MB table's ota_1 is at 0x320000 and that mismatch is
# how the brick kept coming back.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
  idf_build_get_property(build_dir BUILD_DIR)
  partition_table_get_partition_info(ota1_offset "--partition-name ota_1" "offset")
  if(ota1_offset)
    # Absolute path under BUILD_DIR — file(RELATIVE_PATH) inside
    # esptool_py_flash_target_image returns "" for anything else, which
    # is how ota_1 ended up in flasher_args.json with an empty filename.
    get_filename_component(_ota1_image "${build_dir}/neon_link.bin" ABSOLUTE)
    esptool_py_flash_to_partition(flash ota_1 "${_ota1_image}")
  endif()
endif()
