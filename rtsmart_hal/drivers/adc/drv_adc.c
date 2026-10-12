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

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <pthread.h>

#include "drv_adc.h"

typedef enum {
    RT_ADC_CMD_ENABLE,
    RT_ADC_CMD_DISABLE,
} rt_adc_cmd_t;

#define DRV_ADC_DEV "/dev/adc"

static int _drv_adc_fd      = -1;
static int _drv_adc_ref_cnt = 0;

static int drv_adc_chn_state[DRV_ADC_MAX_CHANNEL] = {
    0, 0, 0, 0, 0, 0,
};

static pthread_spinlock_t _drv_adc_lock = (pthread_spinlock_t)0;

int drv_adc_init(void)
{
    if (0x00 > _drv_adc_fd) {
        if (0x00 > (_drv_adc_fd = open(DRV_ADC_DEV, O_RDWR))) {
            printf("[hal_adc]: open device failed\n");
            return -1;
        }
        if (pthread_spin_init(&_drv_adc_lock, 0) != 0) {
            printf("[hal_adc]: initialize lock failed\n");
            close(_drv_adc_fd);
            _drv_adc_fd = -1;
            return -1;
        }
    }
    _drv_adc_ref_cnt++;

    return 0;
}

int drv_adc_deinit(void)
{
    if (_drv_adc_fd >= 0) {
        if (_drv_adc_ref_cnt > 0) {
            _drv_adc_ref_cnt--;
        }

        if (_drv_adc_ref_cnt == 0) {
            for (int channel = 0; channel < DRV_ADC_MAX_CHANNEL; channel++) {
                if (drv_adc_chn_state[channel]) {
                    (void)ioctl(_drv_adc_fd, RT_ADC_CMD_DISABLE, channel);
                }
            }
            close(_drv_adc_fd);
            _drv_adc_fd = -1;

            memset(&drv_adc_chn_state[0], 0, sizeof(drv_adc_chn_state));

            pthread_spin_destroy(&_drv_adc_lock);
        }
    }

    return 0;
}

uint32_t drv_adc_read(int channel)
{
    uint32_t value = 0;
    ssize_t read_size;

    if (0x00 > _drv_adc_fd) {
        printf("[hal_adc]: not inited\n");
        return DRV_ADC_INVALID_VALUE;
    }

    if (channel < 0 || DRV_ADC_MAX_CHANNEL <= channel) {
        printf("[hal_adc]: invalid channel %d\n", channel);
        return DRV_ADC_INVALID_VALUE;
    }

    pthread_spin_lock(&_drv_adc_lock);

    if (0x00 == drv_adc_chn_state[channel]) {
        if (0x00 != ioctl(_drv_adc_fd, RT_ADC_CMD_ENABLE, channel)) {
            printf("[hal_adc]: enable channel failed\n");

            pthread_spin_unlock(&_drv_adc_lock);
            return DRV_ADC_INVALID_VALUE;
        }
        drv_adc_chn_state[channel] = 1;
    }

    if (lseek(_drv_adc_fd, channel, SEEK_SET) < 0) {
        printf("[hal_adc]: select channel failed\n");

        pthread_spin_unlock(&_drv_adc_lock);
        return DRV_ADC_INVALID_VALUE;
    }

    read_size = read(_drv_adc_fd, &value, sizeof(value));
    if (read_size != (ssize_t)sizeof(value)) {
        printf("[hal_adc]: read channel failed\n");

        pthread_spin_unlock(&_drv_adc_lock);
        return DRV_ADC_INVALID_VALUE;
    }

    pthread_spin_unlock(&_drv_adc_lock);

    return value;
}

int drv_adc_raw_to_uv(uint32_t raw, uint32_t ref_uv, uint32_t* uv)
{
    if (raw > DRV_ADC_RESOLUTION || ref_uv == 0 || uv == NULL) {
        return -1;
    }

    *uv = (uint32_t)(((uint64_t)raw * ref_uv + DRV_ADC_RESOLUTION / 2)
                     / DRV_ADC_RESOLUTION);
    return 0;
}

uint32_t drv_adc_read_uv(int channel, uint32_t ref_uv)
{
    uint32_t raw = drv_adc_read(channel);
    uint32_t uv;

    if (raw == DRV_ADC_INVALID_VALUE
        || drv_adc_raw_to_uv(raw, ref_uv, &uv) != 0) {
        return DRV_ADC_INVALID_VALUE;
    }

    return uv;
}
