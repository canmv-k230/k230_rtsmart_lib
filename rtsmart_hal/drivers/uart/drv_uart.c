/* Copyright (c) 2025, Canaan Bright Sight Co., Ltd
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "hal_syscall.h"

#include "drv_uart.h"

#define UART_IOCTL_SET_CONFIG _IOW('U', 0, void*)
#define UART_IOCTL_GET_CONFIG _IOR('U', 1, void*)
#define UART_IOCTL_SEND_BREAK _IOR('U', 2, void*)
#define UART_IOCTL_GET_DTR    _IOR('U', 3, void*)

static const int _drv_uart_inst_type; /**< Unique identifier for UART instance type */
static int       _drv_uart_state[KD_HARD_UART_MAX_NUM]; /**< Tracks usage state of each UART interface */

/**
 * @brief Create a UART driver instance
 * @param id UART interface ID (0 to KD_HARD_UART_MAX_NUM-1)
 * @param dev UART Device Name (used for cdc and lte serial device)
 * @param inst Double pointer to store the created instance
 * @return 0 on success, negative error code on failure:
 *         -1: Invalid parameters
 *         -2: Invalid UART ID
 *         -3: Memory allocation failed
 */
int _drv_uart_inst_create(int id, const char* dev, drv_uart_inst_t** inst)
{
    int  fd = -1;
    char dev_name[64];

    /* Parameter validation */
    if (inst == NULL) {
        return -1;
    }

    if (NULL == dev) {
        /* Check UART ID range */
        if (KD_HARD_UART_MAX_NUM <= id) {
            printf("[hal_uart]: invalid id\n");
            return -2;
        }

        /* Check if UART is already in use */
        if (0x00 != _drv_uart_state[id]) {
            printf("[hal_uart]: uart%d maybe in use\n", id);
        }

        snprintf(dev_name, sizeof(dev_name), "/dev/uart%d", id);
    } else {
        id = -1;

        strncpy(dev_name, dev, sizeof(dev_name) - 1);
    }
    dev_name[sizeof(dev_name) - 1] = '\0';

    /* Clean up existing instance if provided */
    if (*inst) {
        drv_uart_inst_destroy(inst);
        *inst = NULL;
    }

    /* Open UART device */
    if (0 > (fd = open(dev_name, O_RDWR | O_NONBLOCK))) {
        printf("[hal_uart]: open %s failed\n", dev_name);
        return -1;
    }

    /* Allocate and initialize instance */
    *inst = (drv_uart_inst_t*)malloc(sizeof(drv_uart_inst_t));
    if (*inst == NULL) {
        printf("[hal_uart]: malloc instance failed");
        close(fd);
        return -3;
    }
    memset(*inst, 0, sizeof(drv_uart_inst_t));

    /* Initialize instance fields */
    (*inst)->base = (void*)&_drv_uart_inst_type;
    (*inst)->id   = id;
    (*inst)->fd   = fd;

    /* Mark UART as in use */
    if ((0 <= id) && (KD_HARD_UART_MAX_NUM > id)) {
        _drv_uart_state[id] = 1;
    }

    return 0;
}

/**
 * @brief Destroy a UART driver instance
 * @param inst Double pointer to the instance to destroy
 */
void drv_uart_inst_destroy(drv_uart_inst_t** inst)
{
    int id, fd;

    /* Parameter validation */
    if (inst == NULL || *inst == NULL) {
        printf("[hal_uart]: inst not uart inst\n");
        return;
    }

    /* Verify instance type */
    if ((void*)&_drv_uart_inst_type != (*inst)->base) {
        printf("[hal_uart]: inst not uart inst\n");
        return;
    }

    /* Get instance properties */
    fd = (*inst)->fd;
    id = (*inst)->id;

    /* Close file descriptor if open */
    if (0 <= fd) {
        close(fd);
    }

    /* Mark UART as available */
    if ((0 <= id) && (KD_HARD_UART_MAX_NUM > id)) {
        _drv_uart_state[id] = 0;
    }

    /* Free instance memory */
    free(*inst);
    *inst = NULL;
}

/**
 * @brief Read data from UART
 * @param inst UART instance
 * @param buffer Buffer to store read data
 * @param size Number of bytes to read
 * @return Number of bytes read on success, negative error code on failure:
 *         -1: Invalid parameters
 *         -2: Read error
 */
ssize_t drv_uart_read(drv_uart_inst_t* inst, uint8_t* buffer, size_t size)
{
    /* Parameter validation */
    if (inst == NULL || inst->fd == -1 || buffer == NULL) {
        return -1;
    }
    if (0x00 == size) {
        return 0;
    }

    /* Perform read operation */
    ssize_t bytes_read = read(inst->fd, (void*)buffer, size);
    if (bytes_read < 0) {
        return -2;
    }

    return bytes_read;
}

/**
 * @brief Write data to UART
 * @param inst UART instance
 * @param buffer Data to write
 * @param size Number of bytes to write
 * @return Number of bytes written on success, negative error code on failure:
 *         -1: Invalid parameters
 *         -2: Write error
 */
ssize_t drv_uart_write(drv_uart_inst_t* inst, const uint8_t* buffer, size_t size)
{
    /* Parameter validation */
    if (inst == NULL || inst->fd == -1 || buffer == NULL) {
        return -1;
    }
    if (0x00 == size) {
        return 0;
    }

    /* Perform write operation */
    ssize_t bytes_written = write(inst->fd, buffer, size);
    if (bytes_written < 0) {
        return -2;
    }

    return bytes_written;
}

/**
 * @brief Poll UART for read availability
 * @param inst UART instance
 * @param timeout_ms Timeout in milliseconds (-1 = infinite, 0 = non-blocking)
 * @return >0 if data is available, 0 on timeout, negative error code on failure:
 *         -1: Invalid parameters
 *         -errno: Poll error (negative errno value)
 *         -EIO: Device error occurred
 */
int drv_uart_poll(drv_uart_inst_t* inst, int timeout_ms)
{
    /* Parameter validation */
    if (inst == NULL || inst->fd < 0) {
        errno = EINVAL;
        return -1;
    }

    /* Setup poll structure */
    struct pollfd fds = { .fd      = inst->fd,
                          .events  = POLLIN, /* Monitor for read availability */
                          .revents = 0 };

    /* Perform poll operation */
    int ret = poll(&fds, 1, timeout_ms);
    if (ret < 0) {
        return -errno;
    }

    /* Check for device errors */
    if (ret > 0 && (fds.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        return -EIO;
    }

    return ret;
}

/**
 * @brief Check number of bytes available to read
 * @param inst UART instance
 * @return Number of bytes available on success, negative error code on failure:
 *         -1: Invalid parameters
 *         -2: IOCTL error
 */
ssize_t drv_uart_recv_available(drv_uart_inst_t* inst)
{
    /* Parameter validation */
    if (inst == NULL || inst->fd == -1) {
        return -1;
    }

    /* Get available bytes count */
    size_t bytes_available;
    if (ioctl(inst->fd, FIONREAD, &bytes_available) < 0) {
        return -2;
    }

    return bytes_available;
}

/**
 * @brief Check whether the UART DTR (Data Terminal Ready) signal is asserted.
 *
 * This function verifies if the DTR line is active for the specified UART instance.
 * If the DTR signal is not asserted, data transmission to the peer may be blocked.
 *
 * Special case: For hardware UART instances within the valid ID range
 * (0 to KD_HARD_UART_MAX_NUM - 1), the function always returns 1
 * as these ports are considered always ready.
 *
 * @param inst Pointer to the UART driver instance.
 *
 * @return
 *   1  : DTR is asserted (ready to send data)
 *   0  : DTR is not asserted (not ready)
 *  -1  : Invalid parameters (NULL pointer or invalid file descriptor)
 *  -2  : IOCTL error when retrieving DTR state
 */
int drv_uart_is_dtr_asserted(drv_uart_inst_t* inst)
{
    /* Parameter validation */
    if (inst == NULL || inst->fd == -1) {
        return -1;
    }

    int id = inst->id;
    if ((0 <= id) && (KD_HARD_UART_MAX_NUM > id)) {
        return 1;
    }

    int dtr = 0;
    if (ioctl(inst->fd, UART_IOCTL_GET_DTR, &dtr) < 0) {
        return -2;
    }

    return dtr;
}

/**
 * @brief Send a break condition on the UART TX line.
 *
 * This function sends a UART break signal, which forces the TX line to a low
 * logic level for a period longer than a standard data frame. It is useful for
 * signaling special events (e.g., LIN sync, attention request, soft reset).
 *
 * Internally, it issues an IOCTL command (`UART_IOCTL_SEND_BREK`) to the driver,
 * which manipulates the UART_LCR_SBRK bit in the UART line control register.
 *
 * @param inst Pointer to a valid UART instance with an open file descriptor.
 *
 * @return  0 on success
 *         -1 if the instance is invalid or not opened
 *         -2 if the ioctl call failed
 */
int drv_uart_send_break(drv_uart_inst_t* inst)
{
    /* Parameter validation */
    if (inst == NULL || inst->fd == -1) {
        return -1;
    }

    /* Send break via IOCTL */
    if (ioctl(inst->fd, UART_IOCTL_SEND_BREAK, NULL) < 0) {
        return -2;
    }

    return 0;
}

/**
 * @brief Set UART configuration
 * @param inst UART instance
 * @param cfg Configuration structure
 * @return 0 on success, negative error code on failure:
 *         -1: Invalid parameters
 *         -2: IOCTL error
 * @note It can not change bufsz, use @drv_uart_configure_buffer_size instead
 */
int drv_uart_set_config(drv_uart_inst_t* inst, struct uart_configure* cfg)
{
    struct uart_configure curr;

    /* Parameter validation */
    if (inst == NULL || cfg == NULL || inst->fd == -1) {
        return -1;
    }

    if (0x00 != drv_uart_get_config(inst, &curr)) {
        return -2;
    }
    cfg->bufsz = curr.bufsz;

    /* Set configuration via IOCTL */
    if (ioctl(inst->fd, UART_IOCTL_SET_CONFIG, cfg) < 0) {
        return -2;
    }

    return 0;
}

/**
 * @brief Get current UART configuration
 * @param inst UART instance
 * @param cfg Configuration structure to populate
 * @return 0 on success, negative error code on failure:
 *         -1: Invalid parameters
 *         -2: IOCTL error
 */
int drv_uart_get_config(drv_uart_inst_t* inst, struct uart_configure* cfg)
{
    /* Parameter validation */
    if (inst == NULL || cfg == NULL || inst->fd == -1) {
        return -1;
    }

    /* Get configuration via IOCTL */
    if (ioctl(inst->fd, UART_IOCTL_GET_CONFIG, cfg) < 0) {
        return -2;
    }

    return 0;
}

/**
 * @brief Configure the UART buffer size for a given UART device.
 *
 * @note Should call before instance created.
 *
 * @param id   UART device ID (e.g., 0 for UART0, 1 for UART1, etc.).
 * @param size Desired buffer size to set.
 * @return int
 *   - 0    Success.
 *   - -1   Invalid UART ID or Status.
 *   - -2   Device not found or configuration failed.
 */
int drv_uart_configure_buffer_size(int id, uint16_t size)
{
    char        dev_name[64]; // Buffer to store UART device name (e.g., "uart0")
    rt_device_t device = NULL; // Handle for the UART device

    struct uart_configure cfg; // UART configuration structure

    /* Check if the UART ID is within valid range */
    if (KD_HARD_UART_MAX_NUM <= id) {
        printf("[hal_uart]: invalid id\n");
        return -1; // Error: Invalid UART ID
    }

    if (_drv_uart_state[id]) {
        printf("[hal_uart]: change uart device buffersize can not open it\n");
        return -1;
    }

    /* Generate the UART device name (e.g., "uart0" for id=0) */
    snprintf(dev_name, sizeof(dev_name), "uart%d", id);

    /* Find the UART device by name */
    device = rt_device_find(dev_name);
    if (NULL == device) {
        printf("[hal_uart]: can not find device %s\n", dev_name);
        return -2; // Error: Device not found
    }

    /* Get current UART configuration */
    if (0x00 != rt_device_control(device, UART_IOCTL_GET_CONFIG, &cfg)) {
        printf("[hal_uart]: can not get device configure\n");
        return -2; // Error: Failed to read config
    }

    /* Update buffer size in the configuration */
    cfg.bufsz = size;

    /* Apply the new configuration */
    if (0x00 != rt_device_control(device, UART_IOCTL_SET_CONFIG, &cfg)) {
        printf("[hal_uart]: can not set device configure\n");
        return -2; // Error: Failed to set config
    }

    return 0; // Success
}
