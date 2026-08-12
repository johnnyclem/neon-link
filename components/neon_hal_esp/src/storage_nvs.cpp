#include "halesp/storage_nvs.hpp"

#include "esp_log.h"
#include "nvs.h"

namespace halesp {

namespace {
const char* kTag = "storage";
const char* kNamespace = "neon";
}  // namespace

bool StorageNvs::read_blob(const char* key, void* buf, size_t cap,
                           size_t* len) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) {
    return false;
  }
  size_t size = cap;
  const esp_err_t err = nvs_get_blob(h, key, buf, &size);
  nvs_close(h);
  if (err != ESP_OK) {
    return false;
  }
  *len = size;
  return true;
}

bool StorageNvs::erase_all() {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "nvs_open failed: %d", err);
    return false;
  }
  err = nvs_erase_all(h);
  if (err == ESP_OK) {
    err = nvs_commit(h);
  }
  nvs_close(h);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "nvs erase failed: %d", err);
    return false;
  }
  ESP_LOGW(kTag, "namespace \"%s\" erased (factory reset)", kNamespace);
  return true;
}

bool StorageNvs::write_blob(const char* key, const void* data, size_t len) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "nvs_open failed: %d", err);
    return false;
  }
  err = nvs_set_blob(h, key, data, len);
  if (err == ESP_OK) {
    err = nvs_commit(h);
  }
  nvs_close(h);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "nvs write failed: %d", err);
    return false;
  }
  return true;
}

}  // namespace halesp
