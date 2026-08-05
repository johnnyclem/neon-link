#pragma once

#include <cstddef>

#include "neon/config/model.hpp"

namespace neon {

// JSON representation of the configuration, shared by the web editor and
// (later) preset export. Enum fields use short strings ("start"/"bar"/
// "off", "auto"/"link"/"external", "ignore"/"replace"/"merge").
//
// The WiFi password is write-only: encode emits "" so the editor can show
// a placeholder without ever leaking the stored secret.

// Serializes into buf (NUL-terminated). Returns the length written, or 0
// if cap is too small.
size_t config_to_json(const Config& cfg, char* buf, size_t cap);

// Applies a JSON document onto an existing Config — PARTIAL update
// semantics: only fields present in the document are changed, unknown
// fields are ignored, and the result is sanitized. Returns false on
// malformed JSON or wrong top-level type (cfg is left untouched).
bool config_from_json(const char* json, size_t len, Config* cfg);

}  // namespace neon
