#include "blemidi/ble_midi.h"

#include "sdkconfig.h"

#if CONFIG_BT_NIMBLE_ENABLED

#include <atomic>
#include <cstring>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

namespace blemidi {

namespace {

const char* kTag = "ble_midi";

// MIDI over BLE service 03B80E5A-EDE8-4B33-A751-6CE34EC4C700 and data I/O
// characteristic 7772E5DB-3868-4112-A1A9-F2669D106BF3 (little-endian).
const ble_uuid128_t kSvcUuid =
    BLE_UUID128_INIT(0x00, 0xc7, 0xc4, 0x4e, 0xe3, 0x6c, 0x51, 0xa7, 0x33,
                     0x4b, 0xe8, 0xed, 0x5a, 0x0e, 0xb8, 0x03);
const ble_uuid128_t kChrUuid =
    BLE_UUID128_INIT(0xf3, 0x6b, 0x10, 0x9d, 0x66, 0xf2, 0xa9, 0xa1, 0x12,
                     0x41, 0x68, 0x38, 0xdb, 0xe5, 0x72, 0x77);

PacketHandler g_handler = nullptr;
std::atomic<bool> g_running{false};
std::atomic<bool> g_connected{false};
uint8_t g_own_addr_type = BLE_OWN_ADDR_PUBLIC;
uint16_t g_chr_val_handle = 0;

void advertise();

int chr_access(uint16_t, uint16_t, ble_gatt_access_ctxt* ctxt, void*) {
  switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
      uint8_t buf[64];
      uint16_t len = 0;
      if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len) == 0 &&
          g_handler != nullptr && len > 0) {
        g_handler(buf, len);
      }
      return 0;
    }
    case BLE_GATT_ACCESS_OP_READ_CHR:
      // Spec: reads return no payload.
      return 0;
    default:
      return BLE_ATT_ERR_UNLIKELY;
  }
}

const struct ble_gatt_chr_def kChrs[] = {
    {
        .uuid = &kChrUuid.u,
        .access_cb = chr_access,
        .arg = nullptr,
        .descriptors = nullptr,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP |
                 BLE_GATT_CHR_F_NOTIFY,
        .min_key_size = 0,
        .val_handle = &g_chr_val_handle,
        .cpfd = nullptr,
    },
    {},
};

const struct ble_gatt_svc_def kSvcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kSvcUuid.u,
        .includes = nullptr,
        .characteristics = kChrs,
    },
    {},
};

int gap_event(ble_gap_event* event, void*) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        g_connected.store(true);
        ESP_LOGI(kTag, "connected");
      } else {
        advertise();
      }
      return 0;
    case BLE_GAP_EVENT_DISCONNECT:
      g_connected.store(false);
      ESP_LOGI(kTag, "disconnected");
      advertise();
      return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
      advertise();
      return 0;
    default:
      return 0;
  }
}

void advertise() {
  ble_gap_adv_params adv_params = {};
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  ble_hs_adv_fields fields = {};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.uuids128 = const_cast<ble_uuid128_t*>(&kSvcUuid);
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;
  ble_gap_adv_set_fields(&fields);

  ble_hs_adv_fields rsp = {};
  const char* name = ble_svc_gap_device_name();
  rsp.name = reinterpret_cast<const uint8_t*>(name);
  rsp.name_len = static_cast<uint8_t>(std::strlen(name));
  rsp.name_is_complete = 1;
  ble_gap_adv_rsp_set_fields(&rsp);

  const int rc = ble_gap_adv_start(g_own_addr_type, nullptr, BLE_HS_FOREVER,
                                   &adv_params, gap_event, nullptr);
  if (rc != 0 && rc != BLE_HS_EALREADY) {
    ESP_LOGW(kTag, "adv start failed: %d", rc);
  }
}

void on_sync() {
  ble_hs_util_ensure_addr(0);
  ble_hs_id_infer_auto(0, &g_own_addr_type);
  advertise();
}

void host_task(void*) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

}  // namespace

bool start(PacketHandler handler) {
  if (g_running.load()) {
    return true;
  }
  g_handler = handler;
  if (nimble_port_init() != ESP_OK) {
    ESP_LOGE(kTag, "nimble init failed");
    return false;
  }
  ble_hs_cfg.sync_cb = on_sync;
  ble_svc_gap_init();
  ble_svc_gatt_init();
  if (ble_gatts_count_cfg(kSvcs) != 0 || ble_gatts_add_svcs(kSvcs) != 0) {
    ESP_LOGE(kTag, "gatt registration failed");
    return false;
  }
  ble_svc_gap_device_name_set("NEON LINK");
  nimble_port_freertos_init(host_task);
  g_running.store(true);
  ESP_LOGI(kTag, "advertising as NEON LINK");
  return true;
}

void stop() {
  if (!g_running.load()) {
    return;
  }
  nimble_port_stop();
  nimble_port_deinit();
  g_running.store(false);
  g_connected.store(false);
  ESP_LOGI(kTag, "stopped (kill switch)");
}

bool connected() { return g_connected.load(); }

}  // namespace blemidi

#else  // !CONFIG_BT_NIMBLE_ENABLED

namespace blemidi {
bool start(PacketHandler) { return false; }
void stop() {}
bool connected() { return false; }
}  // namespace blemidi

#endif
