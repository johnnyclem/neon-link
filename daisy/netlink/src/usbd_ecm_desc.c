/* Device + string descriptors for the netlink CDC-ECM function, and
 * the USBD handle it runs on (separate from libDaisy's CDC handle name
 * so the two can never collide at link time).
 *
 * VID/PID: pid.codes 0x1209/0x0001 — the openly-licensed TEST pid,
 * appropriate while this is a development build. Swap for a real
 * allocation before shipping hardware (docs/DAISY.md §9). The serial
 * string doubles as the ECM iMACAddress (the config descriptor points
 * its functional descriptor at string index 3), which is how hosts
 * learn their side of the virtual cable without user-string plumbing. */

#include "usbd_ecm.h"

#include "usbd_core.h"
#include "usbd_ctlreq.h"

USBD_HandleTypeDef hUsbDeviceNet;

#define ECM_VID 0x1209U
#define ECM_PID 0x0001U

__ALIGN_BEGIN static uint8_t ecm_device_desc[18] __ALIGN_END = {
    18, USB_DESC_TYPE_DEVICE,
    0x00, 0x02, /* USB 2.0 */
    0x02,       /* class: CDC */
    0x00, 0x00, /* subclass/protocol at interface level */
    0x40,       /* EP0 max packet */
    (uint8_t)(ECM_VID & 0xFFU), (uint8_t)(ECM_VID >> 8),
    (uint8_t)(ECM_PID & 0xFFU), (uint8_t)(ECM_PID >> 8),
    0x00, 0x01, /* bcdDevice 1.00 */
    USBD_IDX_MFC_STR, USBD_IDX_PRODUCT_STR, USBD_IDX_SERIAL_STR,
    0x01, /* one configuration */
};

__ALIGN_BEGIN static uint8_t ecm_langid_desc[4] __ALIGN_END = {
    4, USB_DESC_TYPE_STRING, 0x09, 0x04, /* en-US */
};

__ALIGN_BEGIN static uint8_t ecm_str_buf[128] __ALIGN_END;

/* The host-side MAC as a string descriptor, built in usbd_ecm.c. */
extern uint8_t* ecm_serial_str_descriptor(uint16_t* length);

static uint8_t* ecm_get_device_desc(USBD_SpeedTypeDef speed, uint16_t* length)
{
  (void)speed;
  *length = sizeof(ecm_device_desc);
  return ecm_device_desc;
}

static uint8_t* ecm_get_langid_desc(USBD_SpeedTypeDef speed, uint16_t* length)
{
  (void)speed;
  *length = sizeof(ecm_langid_desc);
  return ecm_langid_desc;
}

static uint8_t* ecm_get_mfc_desc(USBD_SpeedTypeDef speed, uint16_t* length)
{
  (void)speed;
  USBD_GetString((uint8_t*)"NEON LINK", ecm_str_buf, length);
  return ecm_str_buf;
}

static uint8_t* ecm_get_product_desc(USBD_SpeedTypeDef speed, uint16_t* length)
{
  (void)speed;
  USBD_GetString((uint8_t*)"NEON LINK USB Ethernet", ecm_str_buf, length);
  return ecm_str_buf;
}

static uint8_t* ecm_get_serial_desc(USBD_SpeedTypeDef speed, uint16_t* length)
{
  (void)speed;
  return ecm_serial_str_descriptor(length);
}

static uint8_t* ecm_get_config_desc(USBD_SpeedTypeDef speed, uint16_t* length)
{
  (void)speed;
  USBD_GetString((uint8_t*)"USB Ethernet (CDC-ECM)", ecm_str_buf, length);
  return ecm_str_buf;
}

static uint8_t* ecm_get_interface_desc(USBD_SpeedTypeDef speed,
                                       uint16_t* length)
{
  (void)speed;
  USBD_GetString((uint8_t*)"ECM Data", ecm_str_buf, length);
  return ecm_str_buf;
}

USBD_DescriptorsTypeDef ECM_Desc = {
    .GetDeviceDescriptor = ecm_get_device_desc,
    .GetLangIDStrDescriptor = ecm_get_langid_desc,
    .GetManufacturerStrDescriptor = ecm_get_mfc_desc,
    .GetProductStrDescriptor = ecm_get_product_desc,
    .GetSerialStrDescriptor = ecm_get_serial_desc,
    .GetConfigurationStrDescriptor = ecm_get_config_desc,
    .GetInterfaceStrDescriptor = ecm_get_interface_desc,
};
