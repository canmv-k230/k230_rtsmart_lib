/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef RTSMART_NIMBLE_HCI_H
#define RTSMART_NIMBLE_HCI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Select the H:4 character device before calling ble_hci_uart_init(). */
int rtsmart_nimble_hci_set_device(const char *device);

#ifdef __cplusplus
}
#endif

#endif /* RTSMART_NIMBLE_HCI_H */
