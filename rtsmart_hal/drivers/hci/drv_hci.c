/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "drv_hci.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

struct drv_hci_inst {
    int fd;
    pthread_mutex_t tx_lock;
};

static uint16_t drv_hci_get_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static bool drv_hci_tx_packet_valid(const uint8_t *packet, size_t size)
{
    size_t expected;

    if (!packet || size < 2) {
        return false;
    }

    switch (packet[0]) {
    case DRV_HCI_H4_CMD:
        if (size < 4) {
            return false;
        }
        expected = 4 + packet[3];
        break;
    case DRV_HCI_H4_ACL:
        if (size < 5) {
            return false;
        }
        expected = 5 + drv_hci_get_le16(packet + 3);
        break;
    case DRV_HCI_H4_SCO:
        if (size < 4) {
            return false;
        }
        expected = 4 + packet[3];
        break;
    case DRV_HCI_H4_ISO:
        if (size < 5) {
            return false;
        }
        expected = 5 + (drv_hci_get_le16(packet + 3) & 0x3fff);
        break;
    default:
        return false;
    }

    return size == expected;
}

static ssize_t drv_hci_write_once(int fd, const uint8_t *packet, size_t size)
{
    ssize_t result;

    do {
        result = write(fd, packet, size);
    } while (result < 0 && errno == EINTR);
    return result < 0 ? -errno : result;
}

int drv_hci_inst_create(const char *device, drv_hci_inst_t **inst)
{
    drv_hci_inst_t *new_inst;
    int result;
    int fd;

    if (!inst) {
        return -EINVAL;
    }
    if (*inst) {
        drv_hci_inst_destroy(inst);
    }
    if (!device) {
        device = DRV_HCI_DEFAULT_DEVICE;
    }

    fd = open(device, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        return -errno;
    }

    new_inst = calloc(1, sizeof(*new_inst));
    if (!new_inst) {
        close(fd);
        return -ENOMEM;
    }
    new_inst->fd = fd;
    result = pthread_mutex_init(&new_inst->tx_lock, NULL);
    if (result != 0) {
        close(fd);
        free(new_inst);
        return -result;
    }

    *inst = new_inst;
    return 0;
}

void drv_hci_inst_destroy(drv_hci_inst_t **inst)
{
    if (!inst || !*inst) {
        return;
    }

    close((*inst)->fd);
    pthread_mutex_destroy(&(*inst)->tx_lock);
    free(*inst);
    *inst = NULL;
}

ssize_t drv_hci_read(drv_hci_inst_t *inst, uint8_t *buffer, size_t size)
{
    ssize_t result;

    if (!inst || inst->fd < 0 || (!buffer && size)) {
        return -EINVAL;
    }
    do {
        result = read(inst->fd, buffer, size);
    } while (result < 0 && errno == EINTR);

    return result < 0 ? -errno : result;
}

ssize_t drv_hci_write_packet(drv_hci_inst_t *inst, const uint8_t *packet,
                             size_t size, int timeout_ms)
{
    struct pollfd poll_fd;
    ssize_t result;
    int lock_result;
    int poll_result;

    if (!inst || inst->fd < 0 || !drv_hci_tx_packet_valid(packet, size)) {
        return -EINVAL;
    }

    lock_result = pthread_mutex_lock(&inst->tx_lock);
    if (lock_result != 0) {
        return -lock_result;
    }

    result = drv_hci_write_once(inst->fd, packet, size);

    if ((result == -EAGAIN || result == -EWOULDBLOCK) && timeout_ms != 0) {
        poll_fd.fd = inst->fd;
        poll_fd.events = POLLOUT;
        poll_fd.revents = 0;
        do {
            poll_result = poll(&poll_fd, 1, timeout_ms);
        } while (poll_result < 0 && errno == EINTR);

        if (poll_result > 0 && !(poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            result = drv_hci_write_once(inst->fd, packet, size);
        } else if (poll_result == 0) {
            result = -ETIMEDOUT;
        } else {
            result = poll_result < 0 ? -errno : -EIO;
        }
    }

    if (result >= 0 && (size_t)result != size) {
        result = -EIO;
    }

    pthread_mutex_unlock(&inst->tx_lock);
    return result;
}

int drv_hci_poll(drv_hci_inst_t *inst, int timeout_ms)
{
    struct pollfd poll_fd;
    int result;

    if (!inst || inst->fd < 0) {
        return -EINVAL;
    }

    poll_fd.fd = inst->fd;
    poll_fd.events = POLLIN;
    poll_fd.revents = 0;
    do {
        result = poll(&poll_fd, 1, timeout_ms);
    } while (result < 0 && errno == EINTR);

    if (result < 0) {
        return -errno;
    }
    if (result > 0 && (poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        return -EIO;
    }
    return result > 0 && (poll_fd.revents & POLLIN) ? 1 : 0;
}

int drv_hci_get_fd(const drv_hci_inst_t *inst)
{
    return inst ? inst->fd : -1;
}
