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
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h> // Added for clarity on uint8_t
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "drv_tsensor.h"

struct tsensor_dev {
    int                fd;
    pthread_spinlock_t lock;
};

// Global device structure managing the single file descriptor
static struct tsensor_dev ts_dev = { .fd = -1, .lock = (pthread_spinlock_t)0 };

/**
 * @brief Opens the temperature sensor device file if it's not already open.
 * @return The file descriptor on success, or -1 on failure.
 */
static int open_ts_dev()
{
    pthread_spin_lock(&ts_dev.lock);

    // 1. Check if already open
    if (ts_dev.fd >= 0) {
        goto unlock;
    }

    // 2. Attempt to open
    ts_dev.fd = open("/dev/ts", O_RDWR);
    if (ts_dev.fd < 0) {
        printf("[hal_tsensor]: open dev fail: %s (errno: %d)\n", strerror(errno), errno);
    }

unlock:
    pthread_spin_unlock(&ts_dev.lock);

    return ts_dev.fd;
}

/**
 * @brief Reads the temperature from the sensor with retry logic for EINTR.
 * @param temp Pointer to store the temperature (double).
 * @return 0 on success, -1 on failure.
 */
int drv_tsensor_read_temperature(double* temp)
{
    int    fd, ret = 0;
    size_t bytes_to_read = sizeof(*temp);
    size_t bytes_read    = 0;
    char*  buf           = (char*)temp; // Use a char pointer for byte arithmetic

    fd = open_ts_dev();
    if (fd < 0) {
        ret = -1;
        goto out;
    }

    // Loop until all bytes are read
    while (bytes_read < bytes_to_read) {
        ret = read(fd, buf + bytes_read, bytes_to_read - bytes_read);

        if (ret > 0) {
            // Success: bytes were read
            bytes_read += ret;
        } else if (ret == 0) {
            // End-of-file (unexpected for device read)
            printf("[hal_tsensor]: read temperature fail: unexpected EOF\n");
            ret = -1;
            goto out;
        } else { // ret < 0, an error occurred
            if (errno == EINTR) {
                // Interrupted system call (EINTR, errno: 4). Retry the operation.
                continue;
            }

            // A non-recoverable error occurred (e.g., EIO, EBADF)
            printf("[hal_tsensor]: read temperature fail: %s (errno: %d, ret: %d)\n", strerror(errno), errno, ret);
            ret = -1;
            goto out;
        }
    }

    // If we reach here, bytes_read == bytes_to_read.
    ret = 0;

out:
    return ret;
}

/**
 * @brief Sets the operating mode of the temperature sensor (Single/Continuous).
 * @param mode The desired mode (RT_DEVICE_TS_CTRL_MODE_SINGLE or RT_DEVICE_TS_CTRL_MODE_CONTINUUOS).
 * @return 0 on success, -1 on failure.
 */
int drv_tsensor_set_mode(uint8_t mode)
{
    int     fd, ret = 0;
    uint8_t _mode = mode;

    fd = open_ts_dev();
    if (fd < 0) {
        ret = -1;
        goto out;
    }

    if ((_mode != RT_DEVICE_TS_CTRL_MODE_SINGLE) && (_mode != RT_DEVICE_TS_CTRL_MODE_CONTINUUOS)) {
        printf("[hal_tsensor]: unsupport ts mode\n");
        ret = -1;
        goto out;
    }

    ret = ioctl(fd, RT_DEVICE_TS_CTRL_SET_MODE, &_mode);
    if (ret) {
        printf("[hal_tsensor]: ts set mode fail: %s (errno: %d, ret: %d)\n", strerror(errno), errno, ret);
        ret = -1;
        goto out;
    }

out:
    return ret;
}

/**
 * @brief Sets the trimming/calibration value for the sensor.
 * @param trim The desired trim value.
 * @return 0 on success, -1 on failure.
 */
int drv_tsensor_set_trim(uint8_t trim)
{
    int     fd, ret = 0;
    uint8_t _trim = trim;

    fd = open_ts_dev();
    if (fd < 0) {
        ret = -1;
        goto out;
    }

    if (_trim > RT_DEVICE_TS_CTRL_MAX_TRIM) {
        ret = -1;
        printf("[hal_tsensor]: too large ts trim value\n");
        goto out;
    }

    ret = ioctl(fd, RT_DEVICE_TS_CTRL_SET_TRIM, &_trim);
    if (ret) {
        printf("[hal_tsensor]: ts set trim fail: %s (errno: %d, ret: %d)\n", strerror(errno), errno, ret);
        ret = -1;
        goto out;
    }

out:
    return ret;
}

/**
 * @brief Gets the current operating mode of the sensor.
 * @param mode Pointer to store the current mode.
 * @return 0 on success, -1 on failure.
 */
int drv_tsensor_get_mode(uint8_t* mode)
{
    int fd, ret = 0;

    fd = open_ts_dev();
    if (fd < 0) {
        ret = -1;
        goto out;
    }

    ret = ioctl(fd, RT_DEVICE_TS_CTRL_GET_MODE, mode);
    if (ret) {
        printf("[hal_tsensor]: ts get mode fail: %s (errno: %d, ret: %d)\n", strerror(errno), errno, ret);
        ret = -1;
        goto out;
    }
out:
    return ret;
}

/**
 * @brief Gets the current trimming/calibration value.
 * @param trim Pointer to store the current trim value.
 * @return 0 on success, -1 on failure.
 */
int drv_tsensor_get_trim(uint8_t* trim)
{
    int fd, ret = 0;

    fd = open_ts_dev();
    if (fd < 0) {
        ret = -1;
        goto out;
    }

    ret = ioctl(fd, RT_DEVICE_TS_CTRL_GET_TRIM, trim);
    if (ret) {
        printf("[hal_tsensor]: ts get trim fail: %s (errno: %d, ret: %d)\n", strerror(errno), errno, ret);
        ret = -1;
        goto out;
    }
out:
    return ret;
}
