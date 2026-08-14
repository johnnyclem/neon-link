# Compiles the repo's portable firmware core into the Teensy build.
#
# components/neon_core is plain C++17 with no platform includes (the host
# CI job enforces this), so it builds for the i.MX RT1062 unchanged. This
# script points the compiler at the same sources the ESP-IDF target and
# the host tests use instead of vendoring a copy that would drift.

import os

Import("env")

repo_root = os.path.abspath(os.path.join(env["PROJECT_DIR"], ".."))
core_dir = os.path.join(repo_root, "components", "neon_core")
hal_dir = os.path.join(repo_root, "components", "neon_hal")
cjson_dir = os.path.join(repo_root, "third_party", "cjson")

env.Append(
    CPPPATH=[
        os.path.join(core_dir, "include"),
        os.path.join(hal_dir, "include"),
        cjson_dir,
    ]
)

# Recursive: picks up src/*.cpp and src/audio/*.cpp.
env.BuildSources(os.path.join("$BUILD_DIR", "neon_core"), os.path.join(core_dir, "src"))

# The one vendored C dependency (config_json.cpp needs it).
env.BuildSources(
    os.path.join("$BUILD_DIR", "cjson"), cjson_dir, src_filter="+<cJSON.c>"
)
