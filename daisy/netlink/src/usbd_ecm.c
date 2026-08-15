/* USB CDC-ECM class + descriptors for the Daisy netlink build (see
 * usbd_ecm.h). Runs on the ST USBD core compiled into libdaisy.a,
 * reusing its usbd_conf.c low-level (OTG_FS PCD, MSP, speed config);
 * only the FIFO map is re-balanced after init so the notification
 * endpoint gets one (ecm_rebalance_fifos, called by usbnet_daisy.cpp).
 *
 * Endpoints (FS, matching the address plan usbd_conf.c sizes FIFOs
 * for): 0x01 bulk OUT (frames in), 0x81 bulk IN (frames out), 0x82
 * interrupt IN (notifications). Frames are delimited by short packets;
 * a ZLP terminates any frame that is an exact multiple of 64 bytes. */

#include "usbd_ecm.h"

#include "usbd_core.h"
#include "usbd_ctlreq.h"

#include "util/unique_id.h"

#include <string.h>

#define ECM_IN_EP 0x81U
#define ECM_OUT_EP 0x01U
#define ECM_NOTIF_EP 0x82U

#define ECM_FS_MPS 64U
#define ECM_NOTIF_MPS 16U
#define ECM_NOTIF_INTERVAL 16U

/* CDC class-specific request: the only one hosts send an ECM device in
 * practice. Accepted and ignored (we receive all frames anyway). */
#define ECM_SET_ETH_PACKET_FILTER 0x43U

/* --- state ------------------------------------------------------------ */

typedef struct
{
  uint8_t alt; /* data interface alt setting: 1 = host opened the pipe */

  /* RX: one frame being assembled from 64-byte transactions, plus a
   * small queue of completed frames for the netif pump. */
  uint8_t rx_assembly[ECM_MAX_FRAME];
  uint32_t rx_len;
  uint8_t rx_frames[4][ECM_MAX_FRAME];
  uint32_t rx_frame_len[4];
  volatile uint8_t rx_head; /* producer (DataOut) */
  volatile uint8_t rx_tail; /* consumer (pump) */
  uint8_t rx_packet[ECM_FS_MPS];

  /* TX: one frame in flight at a time; ZLP chaser when needed. */
  uint8_t tx_frame[ECM_MAX_FRAME];
  volatile uint8_t tx_busy;
  volatile uint8_t tx_zlp_pending;

  /* Notifications queued at link-up. */
  volatile uint8_t notif_stage; /* 0 idle, 1 send conn, 2 send speed */
  volatile uint8_t notif_busy;
} ECM_HandleTypeDef;

static ECM_HandleTypeDef ecm_handle;

static uint8_t ecm_dev_mac[6];
static uint8_t ecm_host_mac_str[26]; /* USB string desc: 12 UTF-16 digits */
static uint8_t ecm_mac_ready;

/* NetworkConnection (connected) + ConnectionSpeedChange (10 Mbit/s both
 * directions — honest-ish for a FS bulk pipe) notification buffers. */
static uint8_t ecm_notif_conn[8] = {0xA1, 0x00, 0x01, 0x00, 0x01, 0x00,
                                    0x00, 0x00};
static uint8_t ecm_notif_speed[16] = {0xA1, 0x2A, 0x00, 0x00, 0x01, 0x00,
                                      0x08, 0x00, 0x00, 0x96, 0x98, 0x00,
                                      0x00, 0x96, 0x98, 0x00};

/* --- MAC addressing ---------------------------------------------------- */

static uint8_t hex_digit(uint8_t v)
{
  return (uint8_t)(v < 10U ? '0' + v : 'A' + (v - 10U));
}

static void ecm_init_addresses(void)
{
  if (ecm_mac_ready)
  {
    return;
  }
  uint32_t w[3];
  dsy_get_unique_id(&w[0], &w[1], &w[2]);
  const uint32_t mix = w[0] ^ (w[1] * 2654435761U) ^ (w[2] * 40503U);

  /* Locally administered, unicast. Device MAC ends even, the host side
   * of the virtual cable (iMACAddress) is the same +1. */
  ecm_dev_mac[0] = 0x02U;
  ecm_dev_mac[1] = 0x4EU; /* 'N' */
  ecm_dev_mac[2] = 0x4CU; /* 'L' */
  ecm_dev_mac[3] = (uint8_t)(mix >> 16);
  ecm_dev_mac[4] = (uint8_t)(mix >> 8);
  ecm_dev_mac[5] = (uint8_t)(mix & 0xFEU);

  uint8_t host_mac[6];
  memcpy(host_mac, ecm_dev_mac, 6);
  host_mac[5] |= 0x01U;

  ecm_host_mac_str[0] = sizeof(ecm_host_mac_str);
  ecm_host_mac_str[1] = USB_DESC_TYPE_STRING;
  for (int i = 0; i < 6; ++i)
  {
    ecm_host_mac_str[2 + 4 * i] = hex_digit((uint8_t)(host_mac[i] >> 4));
    ecm_host_mac_str[3 + 4 * i] = 0;
    ecm_host_mac_str[4 + 4 * i] = hex_digit((uint8_t)(host_mac[i] & 0x0FU));
    ecm_host_mac_str[5 + 4 * i] = 0;
  }
  ecm_mac_ready = 1U;
}

const uint8_t* ecm_device_mac(void)
{
  ecm_init_addresses();
  return ecm_dev_mac;
}

/* Serial-string accessor for usbd_ecm_desc.c: the host-side MAC doubles
 * as the serial number so the ECM functional descriptor's iMACAddress
 * can point at string index 3 without user-string plumbing. */
uint8_t* ecm_serial_str_descriptor(uint16_t* length)
{
  ecm_init_addresses();
  *length = sizeof(ecm_host_mac_str);
  return ecm_host_mac_str;
}

/* usbd_conf.c sizes the OTG_FS FIFOs for two endpoints (RX 0x80, TX0
 * 0x40, TX1 0x80 — all 320 words spoken for), leaving nothing for the
 * notification endpoint's TX FIFO 2. Re-carve after USBD_Init, before
 * USBD_Start: EP0 control traffic fits in 0x20, bulk IN keeps 0x60 (six
 * packets), notifications get 0x20. */
extern PCD_HandleTypeDef hpcd_USB_OTG_FS;

void ecm_rebalance_fifos(void)
{
  HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_FS, 0x80);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 0, 0x20);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 1, 0x60);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 2, 0x20);
}

/* --- configuration descriptor ------------------------------------------ */

/* 9 config + 9 comm intf + 5 header + 5 union + 13 ethernet + 7 notif
 * EP + 9 data alt0 + 9 data alt1 + 7 + 7 bulk EPs. */
#define ECM_CFG_DESC_LEN 80U

__ALIGN_BEGIN static uint8_t ecm_cfg_desc[ECM_CFG_DESC_LEN] __ALIGN_END = {
    /* Configuration */
    0x09, USB_DESC_TYPE_CONFIGURATION, ECM_CFG_DESC_LEN, 0x00,
    0x02, /* two interfaces */
    0x01, 0x00, 0xC0 /* self powered */, 0x32 /* 100 mA */,

    /* Interface 0: Communications, subclass ECM */
    0x09, USB_DESC_TYPE_INTERFACE, 0x00, 0x00, 0x01, 0x02, 0x06, 0x00, 0x00,
    /* CDC Header functional, 1.10 */
    0x05, 0x24, 0x00, 0x10, 0x01,
    /* CDC Union functional: master 0, slave 1 */
    0x05, 0x24, 0x06, 0x00, 0x01,
    /* CDC Ethernet Networking functional: iMACAddress = serial string
     * (index 3), no statistics, 1514 max segment, no multicast filters,
     * no power filters */
    0x0D, 0x24, 0x0F, USBD_IDX_SERIAL_STR, 0x00, 0x00, 0x00, 0x00,
    (uint8_t)(ECM_MAX_FRAME & 0xFFU), (uint8_t)(ECM_MAX_FRAME >> 8), 0x00,
    0x00, 0x00,
    /* Notification endpoint 0x82, interrupt */
    0x07, USB_DESC_TYPE_ENDPOINT, ECM_NOTIF_EP, 0x03, ECM_NOTIF_MPS, 0x00,
    ECM_NOTIF_INTERVAL,

    /* Interface 1 alt 0: Data class, no endpoints (mandatory idle alt) */
    0x09, USB_DESC_TYPE_INTERFACE, 0x01, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00,
    /* Interface 1 alt 1: Data class, bulk in/out */
    0x09, USB_DESC_TYPE_INTERFACE, 0x01, 0x01, 0x02, 0x0A, 0x00, 0x00, 0x00,
    0x07, USB_DESC_TYPE_ENDPOINT, ECM_OUT_EP, 0x02, ECM_FS_MPS, 0x00, 0x00,
    0x07, USB_DESC_TYPE_ENDPOINT, ECM_IN_EP, 0x02, ECM_FS_MPS, 0x00, 0x00,
};

__ALIGN_BEGIN static uint8_t ecm_dev_qualifier[10] __ALIGN_END = {
    10, USB_DESC_TYPE_DEVICE_QUALIFIER, 0x00, 0x02, 0x02, 0x06, 0x00, 0x40,
    0x01, 0x00,
};

/* --- notifications ------------------------------------------------------ */

static void ecm_push_notification(USBD_HandleTypeDef* pdev)
{
  if (ecm_handle.notif_busy)
  {
    return;
  }
  if (ecm_handle.notif_stage == 1U)
  {
    ecm_handle.notif_busy = 1U;
    (void)USBD_LL_Transmit(pdev, ECM_NOTIF_EP, ecm_notif_conn,
                           sizeof(ecm_notif_conn));
  }
  else if (ecm_handle.notif_stage == 2U)
  {
    ecm_handle.notif_busy = 1U;
    (void)USBD_LL_Transmit(pdev, ECM_NOTIF_EP, ecm_notif_speed,
                           sizeof(ecm_notif_speed));
  }
}

/* --- class callbacks ---------------------------------------------------- */

static uint8_t ecm_class_init(USBD_HandleTypeDef* pdev, uint8_t cfgidx)
{
  (void)cfgidx;
  ecm_init_addresses();
  memset(&ecm_handle, 0, sizeof(ecm_handle));
  pdev->pClassData = &ecm_handle;

  (void)USBD_LL_OpenEP(pdev, ECM_NOTIF_EP, USBD_EP_TYPE_INTR, ECM_NOTIF_MPS);
  pdev->ep_in[ECM_NOTIF_EP & 0x0FU].is_used = 1U;
  return (uint8_t)USBD_OK;
}

static uint8_t ecm_class_deinit(USBD_HandleTypeDef* pdev, uint8_t cfgidx)
{
  (void)cfgidx;
  (void)USBD_LL_CloseEP(pdev, ECM_NOTIF_EP);
  pdev->ep_in[ECM_NOTIF_EP & 0x0FU].is_used = 0U;
  if (ecm_handle.alt == 1U)
  {
    (void)USBD_LL_CloseEP(pdev, ECM_IN_EP);
    pdev->ep_in[ECM_IN_EP & 0x0FU].is_used = 0U;
    (void)USBD_LL_CloseEP(pdev, ECM_OUT_EP);
    pdev->ep_out[ECM_OUT_EP & 0x0FU].is_used = 0U;
  }
  ecm_handle.alt = 0U;
  pdev->pClassData = NULL;
  return (uint8_t)USBD_OK;
}

static void ecm_open_data_eps(USBD_HandleTypeDef* pdev)
{
  (void)USBD_LL_OpenEP(pdev, ECM_IN_EP, USBD_EP_TYPE_BULK, ECM_FS_MPS);
  pdev->ep_in[ECM_IN_EP & 0x0FU].is_used = 1U;
  (void)USBD_LL_OpenEP(pdev, ECM_OUT_EP, USBD_EP_TYPE_BULK, ECM_FS_MPS);
  pdev->ep_out[ECM_OUT_EP & 0x0FU].is_used = 1U;

  ecm_handle.rx_len = 0U;
  ecm_handle.tx_busy = 0U;
  ecm_handle.tx_zlp_pending = 0U;
  (void)USBD_LL_PrepareReceive(pdev, ECM_OUT_EP, ecm_handle.rx_packet,
                               ECM_FS_MPS);

  /* The host brings the interface up once it hears the connection
   * notifications. */
  ecm_handle.notif_stage = 1U;
  ecm_push_notification(pdev);
}

static uint8_t ecm_class_setup(USBD_HandleTypeDef* pdev,
                               USBD_SetupReqTypedef* req)
{
  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS:
      /* SetEthernetPacketFilter and friends: no data stage we care
       * about; status handled by the core. */
      if ((req->bmRequest & 0x80U) == 0U && req->wLength == 0U)
      {
        return (uint8_t)USBD_OK;
      }
      if ((req->bmRequest & 0x80U) != 0U)
      {
        /* No class GET requests are meaningful here; answer zeros. */
        static uint8_t zeros[2] = {0U, 0U};
        (void)USBD_CtlSendData(pdev, zeros,
                               req->wLength < 2U ? req->wLength : 2U);
        return (uint8_t)USBD_OK;
      }
      return (uint8_t)USBD_OK;

    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest)
      {
        case USB_REQ_GET_INTERFACE:
        {
          uint8_t alt =
              (req->wIndex == 1U) ? ecm_handle.alt : 0U;
          (void)USBD_CtlSendData(pdev, &alt, 1U);
          return (uint8_t)USBD_OK;
        }
        case USB_REQ_SET_INTERFACE:
          if (req->wIndex == 1U)
          {
            const uint8_t alt = (uint8_t)req->wValue;
            if (alt == ecm_handle.alt)
            {
              return (uint8_t)USBD_OK;
            }
            if (alt == 1U)
            {
              ecm_handle.alt = 1U;
              ecm_open_data_eps(pdev);
            }
            else
            {
              (void)USBD_LL_CloseEP(pdev, ECM_IN_EP);
              pdev->ep_in[ECM_IN_EP & 0x0FU].is_used = 0U;
              (void)USBD_LL_CloseEP(pdev, ECM_OUT_EP);
              pdev->ep_out[ECM_OUT_EP & 0x0FU].is_used = 0U;
              ecm_handle.alt = 0U;
            }
          }
          return (uint8_t)USBD_OK;
        default:
          break;
      }
      break;

    default:
      break;
  }
  USBD_CtlError(pdev, req);
  return (uint8_t)USBD_FAIL;
}

static uint8_t ecm_class_data_in(USBD_HandleTypeDef* pdev, uint8_t epnum)
{
  if (epnum == (ECM_NOTIF_EP & 0x0FU))
  {
    ecm_handle.notif_busy = 0U;
    if (ecm_handle.notif_stage == 1U)
    {
      ecm_handle.notif_stage = 2U;
      ecm_push_notification(pdev);
    }
    else
    {
      ecm_handle.notif_stage = 0U;
    }
    return (uint8_t)USBD_OK;
  }

  if (epnum == (ECM_IN_EP & 0x0FU))
  {
    if (ecm_handle.tx_zlp_pending)
    {
      ecm_handle.tx_zlp_pending = 0U;
      (void)USBD_LL_Transmit(pdev, ECM_IN_EP, NULL, 0U);
      return (uint8_t)USBD_OK;
    }
    ecm_handle.tx_busy = 0U;
  }
  return (uint8_t)USBD_OK;
}

static uint8_t ecm_class_data_out(USBD_HandleTypeDef* pdev, uint8_t epnum)
{
  if (epnum != (ECM_OUT_EP & 0x0FU))
  {
    return (uint8_t)USBD_OK;
  }
  const uint32_t got = USBD_LL_GetRxDataSize(pdev, epnum);

  if (ecm_handle.rx_len + got <= ECM_MAX_FRAME)
  {
    memcpy(&ecm_handle.rx_assembly[ecm_handle.rx_len], ecm_handle.rx_packet,
           got);
    ecm_handle.rx_len += got;
  }
  else
  {
    /* Oversized: drop the assembly, stay in sync with the delimiting. */
    ecm_handle.rx_len = 0U;
  }

  if (got < ECM_FS_MPS)
  {
    /* Short packet (or ZLP): frame complete. */
    if (ecm_handle.rx_len >= 14U)
    {
      const uint8_t next = (uint8_t)((ecm_handle.rx_head + 1U) % 4U);
      if (next != ecm_handle.rx_tail)
      {
        memcpy(ecm_handle.rx_frames[ecm_handle.rx_head],
               ecm_handle.rx_assembly, ecm_handle.rx_len);
        ecm_handle.rx_frame_len[ecm_handle.rx_head] = ecm_handle.rx_len;
        ecm_handle.rx_head = next;
      } /* else: queue full, drop — IP retransmits. */
    }
    ecm_handle.rx_len = 0U;
  }

  (void)USBD_LL_PrepareReceive(pdev, ECM_OUT_EP, ecm_handle.rx_packet,
                               ECM_FS_MPS);
  return (uint8_t)USBD_OK;
}

static uint8_t* ecm_get_cfg_desc(uint16_t* length)
{
  *length = (uint16_t)sizeof(ecm_cfg_desc);
  return ecm_cfg_desc;
}

static uint8_t* ecm_get_qualifier_desc(uint16_t* length)
{
  *length = (uint16_t)sizeof(ecm_dev_qualifier);
  return ecm_dev_qualifier;
}

USBD_ClassTypeDef USBD_ECM = {
    ecm_class_init,
    ecm_class_deinit,
    ecm_class_setup,
    NULL, /* EP0_TxSent */
    NULL, /* EP0_RxReady */
    ecm_class_data_in,
    ecm_class_data_out,
    NULL, /* SOF */
    NULL,
    NULL,
    ecm_get_cfg_desc, /* HS (FS-only device; same descriptor) */
    ecm_get_cfg_desc,
    ecm_get_cfg_desc,
    ecm_get_qualifier_desc,
#if (USBD_SUPPORT_USER_STRING_DESC == 1U)
    NULL,
#endif
};

/* --- data-plane surface (main loop) ------------------------------------ */

int ecm_link_up(void)
{
  return ecm_handle.alt == 1U;
}

uint32_t ecm_read_frame(uint8_t* buf, uint32_t cap)
{
  if (ecm_handle.rx_tail == ecm_handle.rx_head)
  {
    return 0U;
  }
  const uint8_t tail = ecm_handle.rx_tail;
  uint32_t len = ecm_handle.rx_frame_len[tail];
  if (len > cap)
  {
    len = 0U; /* cannot happen with cap >= ECM_MAX_FRAME; stay safe */
  }
  else
  {
    memcpy(buf, ecm_handle.rx_frames[tail], len);
  }
  ecm_handle.rx_tail = (uint8_t)((tail + 1U) % 4U);
  return len;
}

extern USBD_HandleTypeDef hUsbDeviceNet;

int ecm_write_frame(const uint8_t* data, uint32_t len)
{
  if (!ecm_link_up() || len > ECM_MAX_FRAME)
  {
    return -1;
  }
  if (ecm_handle.tx_busy)
  {
    return -1;
  }
  ecm_handle.tx_busy = 1U;
  memcpy(ecm_handle.tx_frame, data, len);
  ecm_handle.tx_zlp_pending = (len % ECM_FS_MPS == 0U) ? 1U : 0U;
  if (USBD_LL_Transmit(&hUsbDeviceNet, ECM_IN_EP, ecm_handle.tx_frame, len) !=
      USBD_OK)
  {
    ecm_handle.tx_busy = 0U;
    ecm_handle.tx_zlp_pending = 0U;
    return -1;
  }
  return 0;
}
