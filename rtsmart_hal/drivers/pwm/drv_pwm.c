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
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "canmv_misc.h"
#include "drv_pwm.h"

#define DRV_PWM_DEV "/dev/pwm"

#define KD_PWM_CMD_ENABLE   _IOW('P', 0, int)
#define KD_PWM_CMD_DISABLE  _IOW('P', 1, int)
#define KD_PWM_CMD_SET_CFG  _IOW('P', 2, int)
#define KD_PWM_CMD_GET_CFG  _IOW('P', 3, int)
#define KD_PWM_CMD_GET_STAT _IOW('P', 4, int)

#define PWM_CHECK_CHANNEL(chn)                                                                                                 \
    do {                                                                                                                       \
        if ((0 > (chn)) || (5 < (chn))) {                                                                                      \
            printf("[hal_pwm]: invalid channel %d\n", (chn));                                                                  \
            return -1;                                                                                                         \
        }                                                                                                                      \
    } while (0)

struct rt_pwm_configuration {
    uint32_t channel; /* 0-n */
    uint32_t period; /* unit:ns 1ns~4.29s:1Ghz~0.23hz */
    uint32_t pulse; /* unit:ns (pulse<=period) */
};

static int _drv_pwm_fd      = -1;
static int _drv_pwm_ref_cnt = 0;
static uint8_t _drv_pwm_enabled[6];

static int pwm_ioctl(int cmd, void* arg)
{
    if (0 > _drv_pwm_fd) {
        if (0 > (_drv_pwm_fd = open(DRV_PWM_DEV, O_RDWR))) {
            printf("[hal_pwm]: open device failed\n");
            return -1;
        }
    }

    int ret = ioctl(_drv_pwm_fd, cmd, arg);
    if (ret < 0) {
        printf("[hal_pwm]: ioctl failed\n");
    }
    return ret;
}

static int drv_pwm_get_cfg(int channel, uint32_t* period, uint32_t* pulse)
{
    PWM_CHECK_CHANNEL(channel);

    struct rt_pwm_configuration cfg = { .channel = channel, .period = 0, .pulse = 0 };

    if (0 != pwm_ioctl(KD_PWM_CMD_GET_CFG, &cfg)) {
        printf("[hal_pwm]: get cfg failed for channel %d\n", channel);
        return -1;
    }

    if (period) {
        *period = cfg.period;
    }

    if (pulse) {
        *pulse = cfg.pulse;
    }

    return 0;
}

int drv_pwm_set_freq(int channel, uint32_t freq)
{
    PWM_CHECK_CHANNEL(channel);

    if (freq == 0 || freq > NSEC_PER_SEC) {
        printf("[hal_pwm]: invalid frequency %u\n", freq);
        return -1;
    }

    struct rt_pwm_configuration cfg;

    uint32_t old_period, old_pulse;

    if (drv_pwm_get_cfg(channel, &old_period, &old_pulse) != 0) {
        return -1;
    }

    // Set target period (in ns)
    cfg.channel = channel;
    cfg.period  = NSEC_PER_SEC / freq;

    if (cfg.period == 0) {
        printf("[hal_pwm]: frequency %u produces a zero period\n", freq);
        return -1;
    }

    // Preserve original duty ratio using 64-bit math
    cfg.pulse = (old_pulse && old_period) ? (uint64_t)cfg.period * old_pulse / old_period : 0;
    if (cfg.pulse > cfg.period) {
        cfg.pulse = cfg.period;
    }

    if (pwm_ioctl(KD_PWM_CMD_SET_CFG, &cfg) != 0) {
        printf("[hal_pwm]: set frequency failed for channel %d\n", channel);
        return -1;
    }

    return 0;
}

int drv_pwm_get_freq(int channel, uint32_t* freq)
{
    PWM_CHECK_CHANNEL(channel);

    uint32_t period;
    if (0 != drv_pwm_get_cfg(channel, &period, NULL)) {
        return -1;
    }

    if (period == 0) {
        printf("[hal_pwm]: set frequency before duty cycle\n");
        return -1;
    }

    if (freq) {
        *freq = period ? NSEC_PER_SEC / period : 0; // Convert period to frequency
    }

    return 0;
}

int drv_pwm_set_duty(int channel, uint32_t duty)
{
    PWM_CHECK_CHANNEL(channel);

    struct rt_pwm_configuration cfg;

    if (duty > 100) {
        printf("[hal_pwm]: duty cycle must be <= 100%%\n");
        return -1;
    }

    uint32_t period;
    if (0 != drv_pwm_get_cfg(channel, &period, NULL)) {
        return -1;
    }

    cfg.channel = channel;
    cfg.period  = period;
    cfg.pulse   = duty ? ((uint64_t)period * duty) / 100 : 0;

    if (0 != pwm_ioctl(KD_PWM_CMD_SET_CFG, &cfg)) {
        printf("[hal_pwm]: set duty cycle failed for channel %d\n", channel);
        return -1;
    }

    return 0;
}

int drv_pwm_get_duty(int channel, uint32_t* duty)
{
    PWM_CHECK_CHANNEL(channel);

    uint32_t period, pulse;
    if (drv_pwm_get_cfg(channel, &period, &pulse) != 0) {
        return -1;
    }

    if (period == 0) {
        if (duty) {
            *duty = 0;
        }
        return 0;
    }

    if (duty) {
        *duty = pulse ? ((uint64_t)pulse * 100) / period : 0;
    }

    return 0;
}

int drv_pwm_set_duty_u16(int channel, uint16_t duty_u16)
{
    PWM_CHECK_CHANNEL(channel);

    struct rt_pwm_configuration cfg;

    uint32_t period;
    if (0 != drv_pwm_get_cfg(channel, &period, NULL)) {
        return -1;
    }

    cfg.channel = channel;
    cfg.period  = period;
    cfg.pulse   = ((uint64_t)period * duty_u16) >> 16; // Convert 16-bit duty to ns

    if (0 != pwm_ioctl(KD_PWM_CMD_SET_CFG, &cfg)) {
        printf("[hal_pwm]: set duty cycle failed for channel %d\n", channel);
        return -1;
    }

    return 0;
}

int drv_pwm_get_duty_u16(int channel, uint16_t* duty_u16)
{
    PWM_CHECK_CHANNEL(channel);

    uint32_t period, pulse;
    if (drv_pwm_get_cfg(channel, &period, &pulse) != 0) {
        return -1; // Failed to get PWM config
    }

    if (period == 0) {
        if (duty_u16) {
            *duty_u16 = 0; // Avoid division by zero
        }
        return 0;
    }

    if (duty_u16) {
        // Scale pulse width to 16-bit range (0–65535)
        *duty_u16 = (uint16_t)(((uint64_t)pulse * 65535) / period);
    }

    return 0;
}

/**
 * @brief Set PWM duty cycle in nanoseconds (direct high-time setting)
 * @param channel PWM channel index
 * @param pulse_ns High-time duration in nanoseconds (must be ≤ period)
 * @return 0 on success, -1 on error
 */
int drv_pwm_set_duty_ns(int channel, uint32_t pulse_ns)
{
    PWM_CHECK_CHANNEL(channel);

    struct rt_pwm_configuration cfg;
    uint32_t                    period;

    // Get current period (ns)
    if (drv_pwm_get_cfg(channel, &period, NULL) != 0) {
        return -1; // Failed to get period
    }

    // Validate pulse_ns <= period
    if (pulse_ns > period) {
        printf("[hal_pwm]: pulse width must not exceed period\n");
        return -1;
    }

    // Apply settings
    cfg.channel = channel;
    cfg.period  = period;
    cfg.pulse   = pulse_ns; // Directly set high-time in ns

    if (pwm_ioctl(KD_PWM_CMD_SET_CFG, &cfg) != 0) {
        printf("[hal_pwm]: set duty (ns) failed for channel %d\n", channel);
        return -1;
    }

    return 0;
}

/**
 * @brief Get current PWM duty cycle in nanoseconds (actual high-time)
 * @param channel PWM channel index
 * @param pulse_ns Output: High-time in nanoseconds (0 ≤ pulse_ns ≤ period)
 * @return 0 on success, -1 on error
 */
int drv_pwm_get_duty_ns(int channel, uint32_t* pulse_ns)
{
    PWM_CHECK_CHANNEL(channel);

    uint32_t period, pulse;
    if (drv_pwm_get_cfg(channel, &period, &pulse) != 0) {
        return -1; // Failed to get config
    }

    if (pulse_ns) {
        *pulse_ns = pulse; // Directly return high-time (ns)
    }

    return 0;
}

int drv_pwm_enable(int channel)
{
    PWM_CHECK_CHANNEL(channel);

    struct rt_pwm_configuration cfg = { .channel = channel };

    if (0 != pwm_ioctl(KD_PWM_CMD_ENABLE, &cfg)) {
        printf("[hal_pwm]: enable failed for channel %d\n", channel);
        return -1;
    }

    if (!_drv_pwm_enabled[channel]) {
        _drv_pwm_enabled[channel] = 1;
        _drv_pwm_ref_cnt++;
    }
    return 0;
}

int drv_pwm_disable(int channel)
{
    PWM_CHECK_CHANNEL(channel);

    struct rt_pwm_configuration cfg = { .channel = channel };

    if (0 != pwm_ioctl(KD_PWM_CMD_DISABLE, &cfg)) {
        printf("[hal_pwm]: disable failed for channel %d\n", channel);
        return -1;
    }

    if (_drv_pwm_enabled[channel]) {
        _drv_pwm_enabled[channel] = 0;
        _drv_pwm_ref_cnt--;
    }
    if (_drv_pwm_ref_cnt == 0) {
        close(_drv_pwm_fd);
        _drv_pwm_fd      = -1;
    }
    return 0;
}

int drv_pwm_init(void)
{
    // Open device on first init
    if (_drv_pwm_fd < 0) {
        _drv_pwm_fd = open(DRV_PWM_DEV, O_RDWR);
        if (_drv_pwm_fd < 0) {
            printf("[hal_pwm]: init failed - could not open device\n");
            return -1;
        }
    }
    return 0;
}

int drv_pwm_deinit(void)
{
    if (_drv_pwm_fd >= 0) {
        for (int channel = 0; channel < 6; channel++) {
            if (_drv_pwm_enabled[channel]) {
                struct rt_pwm_configuration cfg = { .channel = channel };
                (void)pwm_ioctl(KD_PWM_CMD_DISABLE, &cfg);
            }
        }
        close(_drv_pwm_fd);
        _drv_pwm_fd      = -1;
        _drv_pwm_ref_cnt = 0;
        memset(_drv_pwm_enabled, 0, sizeof(_drv_pwm_enabled));
    }
    return 0;
}
