/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define DRV_HCI_DEFAULT_DEVICE "/dev/hci0"
#define DRV_HCI_AUTO_MAX_DEVICES 64

#define DRV_HCI_H4_CMD 0x01
#define DRV_HCI_H4_ACL 0x02
#define DRV_HCI_H4_SCO 0x03
#define DRV_HCI_H4_EVT 0x04
#define DRV_HCI_H4_ISO 0x05

typedef struct drv_hci_inst drv_hci_inst_t;

/* Opening the device starts the controller; destroying it stops the controller. */
int drv_hci_inst_create(const char *device, drv_hci_inst_t **inst);

/* Open the first available /dev/hciX controller.  When device is non-NULL,
 * the selected path is copied into the supplied buffer. */
int drv_hci_inst_create_auto(drv_hci_inst_t **inst, char *device,
                             size_t device_size);
void drv_hci_inst_destroy(drv_hci_inst_t **inst);

/* RX returns raw H:4 bytes; reads are not packet-aligned, so callers must buffer and parse the stream. */
ssize_t drv_hci_read(drv_hci_inst_t *inst, uint8_t *buffer, size_t size);

/* TX must contain exactly one complete host-to-controller H:4 packet. */
ssize_t drv_hci_write_packet(drv_hci_inst_t *inst, const uint8_t *packet,
                             size_t size, int timeout_ms);

/* Returns 1 when RX data is ready, 0 on timeout, or a negative errno value. */
int drv_hci_poll(drv_hci_inst_t *inst, int timeout_ms);

int drv_hci_get_fd(const drv_hci_inst_t *inst);

#ifdef __cplusplus
}
#endif
