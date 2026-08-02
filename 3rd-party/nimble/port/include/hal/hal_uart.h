/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef RTSMART_NIMBLE_HAL_UART_H
#define RTSMART_NIMBLE_HAL_UART_H

#include <stdint.h>

enum hal_uart_parity {
    HAL_UART_PARITY_NONE,
    HAL_UART_PARITY_ODD,
    HAL_UART_PARITY_EVEN,
};

enum hal_uart_flow_ctl {
    HAL_UART_FLOW_CTL_NONE,
    HAL_UART_FLOW_CTL_RTS_CTS,
};

typedef int (*hal_uart_tx_cb_t)(void *arg);
typedef int (*hal_uart_rx_cb_t)(void *arg, uint8_t data);

int hal_uart_init_cbs(uint32_t port, hal_uart_tx_cb_t tx_cb, void *tx_arg,
                      hal_uart_rx_cb_t rx_cb, void *rx_arg);
int hal_uart_config(uint32_t port, uint32_t baud, uint32_t bits,
                    uint32_t stop, enum hal_uart_parity parity,
                    enum hal_uart_flow_ctl flow);
void hal_uart_start_tx(uint32_t port);
int hal_uart_close(uint32_t port);

#endif /* RTSMART_NIMBLE_HAL_UART_H */
