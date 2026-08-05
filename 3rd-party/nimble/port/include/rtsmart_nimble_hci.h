/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef RTSMART_NIMBLE_HCI_H
#define RTSMART_NIMBLE_HCI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Select the H:4 character device before calling ble_hci_uart_init(). Passing
 * NULL explicitly clears a previous selection and restores automatic discovery
 * of the first available /dev/hciX controller. */
int rtsmart_nimble_hci_set_device(const char *device);

/* Returns the selected path after NimBLE has initialized the HCI transport. */
const char *rtsmart_nimble_hci_get_device(void);

#ifdef __cplusplus
}
#endif

#endif /* RTSMART_NIMBLE_HCI_H */
