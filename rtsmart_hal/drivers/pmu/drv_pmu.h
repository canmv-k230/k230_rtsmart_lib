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
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct drv_pmu_inst drv_pmu_inst_t;

/* RTC alarm programming needs at least two seconds of lead time. */
#define DRV_PMU_POWER_CYCLE_MIN_DELAY_S 2U

int drv_pmu_inst_create(drv_pmu_inst_t **inst);
void drv_pmu_inst_destroy(drv_pmu_inst_t **inst);

/* Power-key shutdown request handling. Short presses are consumed in-kernel. */
int drv_pmu_key_register_notify(drv_pmu_inst_t *inst, int signo);
int drv_pmu_key_unregister_notify(drv_pmu_inst_t *inst);
int drv_pmu_key_wait_shutdown(drv_pmu_inst_t *inst, int timeout_ms);
/* Confirm shutdown after user-space cleanup is complete. */
int drv_pmu_key_confirm_shutdown(drv_pmu_inst_t *inst);

/* Shut down immediately; this deliberately bypasses the long-press policy. */
int drv_pmu_shutdown_now(drv_pmu_inst_t *inst);

/* Read the current level of the configured shutdown wakeup pad. */
int drv_pmu_wakeup_pad_get_level(drv_pmu_inst_t *inst, uint32_t pad,
                                 int *level);
int drv_pmu_wakeup_source_get(drv_pmu_inst_t *inst, char *name,
                              size_t name_size);

/*
 * Schedule shutdown after shutdown_after_s seconds, then power on after
 * another poweron_after_s seconds.
 */
int drv_pmu_rtc_schedule_power_cycle(drv_pmu_inst_t *inst,
                                     uint32_t shutdown_after_s,
                                     uint32_t poweron_after_s);
int drv_pmu_rtc_cancel_power_cycle(drv_pmu_inst_t *inst);

#ifdef __cplusplus
}
#endif
