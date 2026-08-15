#ifndef NEON_USBD_ECM_H
#define NEON_USBD_ECM_H

/* USB CDC-ECM (Ethernet Control Model) device class on the ST USBD
 * core: the Daisy enumerates as a USB Ethernet adapter and the host
 * bridges it onto the LAN (macOS and Linux support ECM natively;
 * Windows needs a class driver — documented in docs/DAISY.md).
 *
 * The class moves raw Ethernet frames over one bulk pair, delimited by
 * short packets, plus the NetworkConnection / ConnectionSpeedChange
 * notifications on an interrupt endpoint that hosts require before
 * they bring the interface up. The lwIP netif in usbnet_daisy.cpp is
 * the only consumer. */

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_def.h"

#include <stdint.h>

extern USBD_ClassTypeDef USBD_ECM;
extern USBD_DescriptorsTypeDef ECM_Desc;

/* Ethernet frame ceiling (1500 payload + 14 header). */
#define ECM_MAX_FRAME 1514U

/* The device's own MAC (what lwIP uses). The host-side MAC — the other
 * end of the virtual cable, reported in iMACAddress — is this +1 in the
 * last octet. Both are locally-administered, derived from the MCU UID.
 * Valid after USBD_ECM registration (ecm_init_addresses runs then). */
const uint8_t* ecm_device_mac(void);

/* Re-carve the OTG_FS FIFO map so the notification endpoint gets a TX
 * FIFO (libDaisy's usbd_conf.c only budgets for two IN endpoints). Call
 * between USBD_Init and USBD_Start. */
void ecm_rebalance_fifos(void);

/* Data-plane surface for the netif glue. All main-loop context. */

/* True once the host has selected the data interface's alt setting 1
 * (its "cable plugged" moment). */
int ecm_link_up(void);

/* Fetch a complete received frame into buf (cap >= ECM_MAX_FRAME).
 * Returns the frame length, or 0 when none is pending. */
uint32_t ecm_read_frame(uint8_t* buf, uint32_t cap);

/* Queue one frame for transmission. Returns 0 on success, -1 while the
 * previous transmit is still in flight (drop and let IP retry). */
int ecm_write_frame(const uint8_t* data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* NEON_USBD_ECM_H */
