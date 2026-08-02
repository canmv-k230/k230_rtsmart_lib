/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hal/hal_uart.h"
#include "rtsmart_nimble_hci.h"

#include "drv_hci.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define HCI_DEVICE_PATH_MAX 128
#define HCI_RX_BUFFER_SIZE 512
#define HCI_TX_PACKET_MAX 1024

static drv_hci_inst_t *hci_inst;
static pthread_t hci_rx_thread;
static bool hci_rx_thread_started;
static volatile bool hci_rx_running;
static pthread_mutex_t hci_tx_lock = PTHREAD_MUTEX_INITIALIZER;
static char hci_device[HCI_DEVICE_PATH_MAX] = DRV_HCI_DEFAULT_DEVICE;

static hal_uart_tx_cb_t tx_callback;
static void *tx_callback_arg;
static hal_uart_rx_cb_t rx_callback;
static void *rx_callback_arg;

static size_t hci_packet_size(const uint8_t *packet, size_t length)
{
    if (length == 0) {
        return 0;
    }

    switch (packet[0]) {
    case DRV_HCI_H4_CMD:
        return length >= 4 ? 4 + packet[3] : 0;
    case DRV_HCI_H4_ACL:
        return length >= 5 ? 5 + packet[3] + ((size_t)packet[4] << 8) : 0;
    case DRV_HCI_H4_SCO:
        return length >= 4 ? 4 + packet[3] : 0;
    case DRV_HCI_H4_ISO:
        return length >= 5 ?
            5 + ((packet[3] + ((size_t)packet[4] << 8)) & 0x3fff) : 0;
    default:
        return SIZE_MAX;
    }
}

int rtsmart_nimble_hci_set_device(const char *device)
{
    size_t length;

    if (!device) {
        return -EINVAL;
    }
    if (hci_inst) {
        return -EBUSY;
    }
    length = strlen(device);
    if (length == 0 || length >= sizeof(hci_device)) {
        return -EINVAL;
    }
    memcpy(hci_device, device, length + 1);
    return 0;
}

static void *hci_rx_worker(void *arg)
{
    uint8_t buffer[HCI_RX_BUFFER_SIZE];

    (void)arg;
    while (hci_rx_running) {
        ssize_t length;
        int ready = drv_hci_poll(hci_inst, 100);

        if (ready == 0) {
            continue;
        }
        if (ready < 0) {
            if (hci_rx_running) {
                fprintf(stderr, "nimble: HCI poll failed: %d\n", ready);
            }
            break;
        }
        length = drv_hci_read(hci_inst, buffer, sizeof(buffer));
        if (length == -EAGAIN || length == -EWOULDBLOCK) {
            continue;
        }
        if (length < 0) {
            fprintf(stderr, "nimble: HCI read failed: %zd\n", length);
            break;
        }
        for (ssize_t index = 0; index < length; ++index) {
            if (rx_callback) {
                rx_callback(rx_callback_arg, buffer[index]);
            }
        }
    }
    return NULL;
}

int hal_uart_init_cbs(uint32_t port, hal_uart_tx_cb_t tx_cb, void *tx_arg,
                      hal_uart_rx_cb_t rx_cb, void *rx_arg)
{
    (void)port;
    tx_callback = tx_cb;
    tx_callback_arg = tx_arg;
    rx_callback = rx_cb;
    rx_callback_arg = rx_arg;
    return tx_cb && rx_cb ? 0 : -EINVAL;
}

int hal_uart_config(uint32_t port, uint32_t baud, uint32_t bits,
                    uint32_t stop, enum hal_uart_parity parity,
                    enum hal_uart_flow_ctl flow)
{
    int result;

    (void)port;
    (void)baud;
    (void)bits;
    (void)stop;
    (void)parity;
    (void)flow;
    if (hci_inst) {
        return 0;
    }

    result = drv_hci_inst_create(hci_device, &hci_inst);
    if (result != 0) {
        return result;
    }
    hci_rx_running = true;
    result = pthread_create(&hci_rx_thread, NULL, hci_rx_worker, NULL);
    if (result != 0) {
        hci_rx_running = false;
        drv_hci_inst_destroy(&hci_inst);
        return -result;
    }
    hci_rx_thread_started = true;
    return 0;
}

void hal_uart_start_tx(uint32_t port)
{
    uint8_t packet[HCI_TX_PACKET_MAX];
    size_t expected = 0;
    size_t length = 0;
    int byte;
    ssize_t result;

    (void)port;
    pthread_mutex_lock(&hci_tx_lock);
    while (length < sizeof(packet) &&
           (byte = tx_callback(tx_callback_arg)) >= 0) {
        packet[length++] = (uint8_t)byte;
        expected = hci_packet_size(packet, length);
        if (expected == SIZE_MAX || expected > sizeof(packet)) {
            break;
        }
        if (expected != 0 && length == expected) {
            result = drv_hci_write_packet(hci_inst, packet, length, 1000);
            if (result != (ssize_t)length) {
                fprintf(stderr, "nimble: HCI write failed: %zd\n", result);
            }
            pthread_mutex_unlock(&hci_tx_lock);
            return;
        }
    }
    fprintf(stderr, "nimble: invalid HCI TX packet length: %zu\n", length);
    pthread_mutex_unlock(&hci_tx_lock);
}

int hal_uart_close(uint32_t port)
{
    (void)port;
    hci_rx_running = false;
    if (hci_rx_thread_started) {
        pthread_join(hci_rx_thread, NULL);
        hci_rx_thread_started = false;
    }
    drv_hci_inst_destroy(&hci_inst);
    return 0;
}
