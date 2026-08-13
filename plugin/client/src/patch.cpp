#include "neon/client/patch.hpp"

#include "cJSON.h"

namespace neon::client {

void JsonPatch::setEngineLatency(int32_t us) {
  has_latency_ = true;
  latency_us_ = us;
}

void JsonPatch::setMidiNudge(int32_t us) {
  has_nudge_ = true;
  midi_nudge_us_ = us;
}

void JsonPatch::setQuantum(uint32_t beats) {
  has_quantum_ = true;
  quantum_ = beats;
}

void JsonPatch::setBleEnabled(bool on) {
  has_ble_ = true;
  ble_enabled_ = on;
}

void JsonPatch::setTransportGating(bool on) {
  has_gating_ = true;
  transport_gating_ = on;
}

void JsonPatch::setClockEnabled(int index, bool on) {
  if (index < 0 || index > 3) {
    return;
  }
  clocks_[index].has_enabled = true;
  clocks_[index].enabled = on;
  if (index > clock_hi_) {
    clock_hi_ = index;
  }
}

void JsonPatch::setClockShuffle(int index, uint8_t pct) {
  if (index < 0 || index > 3) {
    return;
  }
  clocks_[index].has_shuffle = true;
  clocks_[index].shuffle_pct = pct;
  if (index > clock_hi_) {
    clock_hi_ = index;
  }
}

void JsonPatch::mergeObject(const char* json, size_t len) {
  if (json == nullptr || len == 0) {
    return;
  }
  merge_.assign(json, len);
}

bool JsonPatch::empty() const {
  return !has_latency_ && !has_nudge_ && !has_quantum_ && !has_ble_ &&
         !has_gating_ && clock_hi_ < 0 && merge_.empty();
}

std::string JsonPatch::toJson() const {
  cJSON* root = cJSON_CreateObject();
  if (root == nullptr) {
    return "{}";
  }

  if (!merge_.empty()) {
    cJSON* extra = cJSON_ParseWithLength(merge_.c_str(), merge_.size());
    if (extra != nullptr && cJSON_IsObject(extra)) {
      cJSON* child = extra->child;
      while (child != nullptr) {
        cJSON* next = child->next;
        cJSON_DetachItemViaPointer(extra, child);
        cJSON_AddItemToObject(root, child->string, child);
        child = next;
      }
    }
    cJSON_Delete(extra);
  }

  if (has_latency_ || has_gating_ || clock_hi_ >= 0) {
    cJSON* engine = cJSON_GetObjectItemCaseSensitive(root, "engine");
    if (engine == nullptr || !cJSON_IsObject(engine)) {
      engine = cJSON_AddObjectToObject(root, "engine");
    }
    if (has_latency_) {
      cJSON_AddNumberToObject(engine, "latency_us", latency_us_);
    }
    if (has_gating_) {
      cJSON_AddBoolToObject(engine, "transport_gating",
                            transport_gating_ ? 1 : 0);
    }
    if (clock_hi_ >= 0) {
      cJSON* clocks = cJSON_AddArrayToObject(engine, "clocks");
      for (int i = 0; i <= clock_hi_; ++i) {
        cJSON* o = cJSON_CreateObject();
        if (clocks_[i].has_enabled) {
          cJSON_AddBoolToObject(o, "enabled", clocks_[i].enabled ? 1 : 0);
        }
        if (clocks_[i].has_shuffle) {
          cJSON_AddNumberToObject(o, "shuffle_pct", clocks_[i].shuffle_pct);
        }
        cJSON_AddItemToArray(clocks, o);
      }
    }
  }

  if (has_nudge_) {
    cJSON_AddNumberToObject(root, "midi_nudge_us", midi_nudge_us_);
  }
  if (has_quantum_) {
    cJSON_AddNumberToObject(root, "quantum", quantum_);
  }
  if (has_ble_) {
    cJSON* ble = cJSON_GetObjectItemCaseSensitive(root, "ble");
    if (ble == nullptr || !cJSON_IsObject(ble)) {
      ble = cJSON_AddObjectToObject(root, "ble");
    }
    cJSON_AddBoolToObject(ble, "enabled", ble_enabled_ ? 1 : 0);
  }

  char* printed = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (printed == nullptr) {
    return "{}";
  }
  std::string out(printed);
  cJSON_free(printed);
  return out;
}

}  // namespace neon::client
