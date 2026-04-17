/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
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
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct drv_pmu_inst drv_pmu_inst_t;
typedef uint32_t drv_pmu_event_t;

#define DRV_PMU_EVENT_LONG_PRESS                0x00000001U
#define DRV_PMU_EVENT_KEY_RELEASE               0x00000002U

int drv_pmu_inst_create(drv_pmu_inst_t **inst);
void drv_pmu_inst_destroy(drv_pmu_inst_t **inst);

int drv_pmu_register_notify(drv_pmu_inst_t *inst, int signo);
int drv_pmu_unregister_notify(drv_pmu_inst_t *inst);

int drv_pmu_wait_event(drv_pmu_inst_t *inst, drv_pmu_event_t *event,
                       int timeout_ms);
int drv_pmu_ack_shutdown(drv_pmu_inst_t *inst);

/*
 * Schedule shutdown after shutdown_after_s seconds, then power on after
 * another poweron_after_s seconds.
 */
int drv_pmu_schedule_power_cycle(drv_pmu_inst_t *inst,
                                 uint32_t shutdown_after_s,
                                 uint32_t poweron_after_s);
int drv_pmu_cancel_power_cycle(drv_pmu_inst_t *inst);

static inline int drv_pmu_event_has_long_press(drv_pmu_event_t event)
{
    return (event & DRV_PMU_EVENT_LONG_PRESS) != 0U;
}

static inline int drv_pmu_event_has_key_release(drv_pmu_event_t event)
{
    return (event & DRV_PMU_EVENT_KEY_RELEASE) != 0U;
}

#ifdef __cplusplus
}
#endif
