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
#include <string.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <pthread.h>

#define DRV_WDT_DEV "/dev/watchdog1"

#define KD_DEVICE_CTRL_WDT_START       _IOW('W', 1, int) /* start watchdog */
#define KD_DEVICE_CTRL_WDT_KEEPALIVE   _IOW('W', 2, int) /* refresh watchdog */
#define KD_DEVICE_CTRL_WDT_SET_TIMEOUT _IOW('W', 3, int) /* set timeout(in seconds) */
#define KD_DEVICE_CTRL_WDT_GET_TIMEOUT _IOW('W', 4, int) /* get timeout(in seconds) */

#if 0
#define KD_DEVICE_CTRL_WDT_GET_TIMELEFT   _IOW('W', 5, int) /* future: get the left time before reboot */
#define KD_DEVICE_CTRL_WDT_SET_PRETIMEOUT _IOW('W', 6, int) /* future: set pretimeout(in seconds) */
#endif

static int _drv_wdt_fd = -1;

int wdt_close(void)
{
    if (_drv_wdt_fd >= 0) {
        close(_drv_wdt_fd);
        _drv_wdt_fd = -1;
    }

    return 0;
}

static int wdt_ioctl(int cmd, void* arg)
{
    if (0 > _drv_wdt_fd) {
        if (0 > (_drv_wdt_fd = open(DRV_WDT_DEV, O_RDWR))) {

            printf("[hal_wdt]: open wdt device failed\n");
            return -1;
        }
    }

    return ioctl(_drv_wdt_fd, cmd, arg);
}

int wdt_set_timeout(uint32_t timeout_sec)
{
    uint32_t _timeout_sec = timeout_sec;

    if (0x00 != wdt_ioctl(KD_DEVICE_CTRL_WDT_SET_TIMEOUT, &_timeout_sec)) {
        printf("[hal_wdt]: set wdt timeout failed\n");
        return -1;
    }

    return 0;
}

uint32_t wdt_get_timeout(void)
{
    uint64_t timeout_sec;

    if (0x00 != wdt_ioctl(KD_DEVICE_CTRL_WDT_GET_TIMEOUT, &timeout_sec)) {
        printf("[hal_wdt]: get wdt timeout failed\n");
        return -1;
    }

    return (uint32_t)timeout_sec;
}

int wdt_start()
{
    if (0x00 != wdt_ioctl(KD_DEVICE_CTRL_WDT_START, NULL)) {
        printf("[hal_wdt]: start wdt failed\n");
        return -1;
    }
    return 0;
}

int wdt_stop(void) { return 0; }

int wdt_feed()
{
    if (0x00 != wdt_ioctl(KD_DEVICE_CTRL_WDT_KEEPALIVE, NULL)) {
        printf("[hal_wdt]: feed wdt failed\n");
        return -1;
    }
    return 0;
}
