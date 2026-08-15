// The bridge between the CDC-ECM function (usbd_ecm.c) and lwIP: one
// Ethernet netif whose wire is the USB bulk pipe, DHCP + AutoIP for an
// address once the host bridges us, IGMP for Link's discovery group,
// and an mDNS responder for <device-name>.local. Everything runs on the
// main loop — lwIP is compiled NO_SYS with no protection layer, so no
// call in this file may happen in interrupt context (the USB ISR only
// moves bytes into usbd_ecm.c's queues; frames enter lwIP here).

#include "usbnet_daisy.h"

#include "stm32h7xx_hal.h"
#include "usbd_core.h"
#include "usbd_ecm.h"

#include "httpd_netlink.h"
#include "timebase_daisy.h"

#include "lwip/apps/mdns.h"
#include "lwip/autoip.h"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/sys.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

#include <cstring>

extern USBD_HandleTypeDef hUsbDeviceNet;
extern PCD_HandleTypeDef hpcd_USB_OTG_FS;

namespace usbnet {
namespace {

struct netif g_netif;
char g_hostname[32] = "neon-link";
bool g_ecm_up = false;

// One frame of TX flattening (pbuf chains) and RX staging.
uint8_t g_tx_frame[ECM_MAX_FRAME];
uint8_t g_rx_frame[ECM_MAX_FRAME];

// A full-size frame takes ~1.3 ms on the FS bulk pipe, so back-to-back
// sends (TCP bursts from the web editor) routinely find the previous
// one in flight. Wait bounded-briefly for the pipe instead of dropping:
// pulse timing lives in ISRs and is unaffected by a short main-loop
// stall, while dropped segments cost 200 ms+ TCP retransmits.
constexpr int64_t kTxWaitUs = 2000;

err_t netlink_linkoutput(struct netif* nf, struct pbuf* p) {
  (void)nf;
  if (p->tot_len > ECM_MAX_FRAME) {
    return ERR_IF;
  }
  const uint16_t len = pbuf_copy_partial(p, g_tx_frame, p->tot_len, 0);
  if (len != p->tot_len) {
    return ERR_IF;
  }
  const int64_t give_up = daisy_now_us() + kTxWaitUs;
  while (ecm_write_frame(g_tx_frame, len) != 0) {
    if (!ecm_link_up() || daisy_now_us() > give_up) {
      // Drop; UDP peers tolerate loss and TCP retransmits.
      return ERR_IF;
    }
  }
  return ERR_OK;
}

err_t netlink_netif_init(struct netif* nf) {
  nf->name[0] = 'u';
  nf->name[1] = 's';
  nf->hostname = g_hostname;
  nf->mtu = 1500;
  nf->hwaddr_len = 6;
  std::memcpy(nf->hwaddr, ecm_device_mac(), 6);
  nf->flags = NETIF_FLAG_ETHARP | NETIF_FLAG_BROADCAST | NETIF_FLAG_IGMP;
  nf->output = etharp_output;
  nf->linkoutput = netlink_linkoutput;
  return ERR_OK;
}

}  // namespace

void init(const char* hostname) {
  set_hostname(hostname);

  lwip_init();
  netif_add(&g_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4, nullptr,
            netlink_netif_init, ethernet_input);
  netif_set_default(&g_netif);
  netif_set_up(&g_netif);
  mdns_resp_init();
  mdns_resp_add_netif(&g_netif, g_hostname);

  // USB last, so the first frames out of the host always find the netif
  // ready. usbd_conf.c (linked from libdaisy.a) does the PCD/MSP work;
  // the FIFO re-carve slots in between init and start (usbd_ecm.h).
  USBD_Init(&hUsbDeviceNet, &ECM_Desc, DEVICE_FS);
  USBD_RegisterClass(&hUsbDeviceNet, &USBD_ECM);
  ecm_rebalance_fifos();
  USBD_Start(&hUsbDeviceNet);

  // usbd_conf.c enables OTG_FS at NVIC priority 0, above everything
  // that matters more. Re-rank below the pulse emitter (TIM5, 4), MIDI
  // (TIM4, 6), and audio (DMA1, 8): a delayed USB ISR only slows
  // network throughput — FS hardware NAKs until we drain — while a
  // delayed pulse edge is an audible miss.
  HAL_NVIC_SetPriority(OTG_FS_IRQn, 10, 0);
}

void poll(int64_t now_us) {
  (void)now_us;

  const bool up = ecm_link_up() != 0;
  if (up != g_ecm_up) {
    g_ecm_up = up;
    if (up) {
      netif_set_link_up(&g_netif);
      // (Re)acquire an address each time the host plugs the virtual
      // cable; AutoIP coop kicks in when no DHCP server answers.
      dhcp_start(&g_netif);
    } else {
      netif_set_link_down(&g_netif);
    }
  }

  // Feed received frames into lwIP. Bounded per pass to match the ECM
  // RX queue depth — the pump can never spin here.
  for (int i = 0; i < 4; ++i) {
    const uint32_t len = ecm_read_frame(g_rx_frame, sizeof(g_rx_frame));
    if (len == 0) {
      break;
    }
    struct pbuf* p = pbuf_alloc(PBUF_RAW, (uint16_t)len, PBUF_POOL);
    if (p == nullptr) {
      break;
    }
    pbuf_take(p, g_rx_frame, (uint16_t)len);
    if (g_netif.input(p, &g_netif) != ERR_OK) {
      pbuf_free(p);
    }
  }

  sys_check_timeouts();
}

void set_hostname(const char* hostname) {
  if (hostname == nullptr || hostname[0] == '\0') {
    return;
  }
  std::strncpy(g_hostname, hostname, sizeof(g_hostname) - 1);
  g_hostname[sizeof(g_hostname) - 1] = '\0';
  if (g_netif.hostname != nullptr) {  // past netif_add
    mdns_resp_rename_netif(&g_netif, g_hostname);
  }
}

bool link_up() { return g_ecm_up; }

bool has_ip() {
  return netif_is_up(&g_netif) &&
         !ip4_addr_isany_val(*netif_ip4_addr(&g_netif));
}

void primary_ip(char* out, size_t cap) {
  if (cap == 0) {
    return;
  }
  out[0] = '\0';
  if (has_ip()) {
    ip4addr_ntoa_r(netif_ip4_addr(&g_netif), out, (int)cap);
  }
}

}  // namespace usbnet

// --- C-linkage glue ----------------------------------------------------

extern "C" {

// libDaisy's usb.cpp owns this vector normally, but nothing in this
// build references UsbHandle, so its object never leaves libdaisy.a and
// the definition here is the only one.
void OTG_FS_IRQHandler(void) { HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS); }

// lwIP's NO_SYS millisecond clock.
u32_t sys_now(void) { return (u32_t)(daisy_now_us() / 1000); }

// LWIP_RAND (DHCP xids, AutoIP probe timing, mDNS jitter). Seeded like
// the Link platform's Random.hpp: cycle counter + microsecond clock.
unsigned int neon_netlink_rand(void) {
  static uint32_t state = 0;
  if (state == 0) {
    volatile uint32_t* const cyccnt = reinterpret_cast<uint32_t*>(0xE0001004);
    state = static_cast<uint32_t>(daisy_now_us()) ^ *cyccnt ^ 0x6C078965u;
    if (state == 0) {
      state = 1;
    }
  }
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

// The Link platform's interface scanner (ScanIpIfAddrs.hpp): our one
// address, or 0 while the bridge is down.
uint32_t neon_netlink_ipv4_hostorder(void) {
  if (!usbnet::has_ip()) {
    return 0;
  }
  return lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(&usbnet::g_netif)));
}

// Strong overrides of main.cpp's weak no-op net hooks: this is the seam
// that turns the netlink build's networking on without forking main.
void neon_daisy_net_init(const char* hostname) {
  usbnet::init(hostname);
  webui::init();
}

void neon_daisy_net_poll(int64_t now_us) {
  usbnet::poll(now_us);
  webui::poll(now_us);
}

bool neon_daisy_net_reboot_requested(void) { return webui::reboot_requested(); }

uint8_t neon_daisy_net_active(void) {
  return usbnet::has_ip() ? 1 : 0;  // 1 = "ethernet" in the status model
}

void neon_daisy_net_ip(char* out, unsigned cap) {
  usbnet::primary_ip(out, cap);
}

}  // extern "C"
