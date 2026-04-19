/*
 *  Portable interface to the CPU cycle counter
 *
 *  Copyright The Mbed TLS Contributors
 *  SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later
 */

#include "mbedtls/build_info.h"

#if defined(MBEDTLS_TIMING_C) && defined(MBEDTLS_TIMING_ALT)

#include "drv_timer.h"
#include "hal_utils.h"
#include "mbedtls/platform.h"
#include "mbedtls/timing.h"

struct _hr_time {
    uint64_t start;
};

unsigned long mbedtls_timing_get_timer(struct mbedtls_timing_hr_time* val, int reset)
{
    struct _hr_time* t = (struct _hr_time*)val;

    if (reset) {
        t->start = utils_cpu_ticks_ms();
        return 0;
    } else {
        uint64_t now   = utils_cpu_ticks_ms();
        uint64_t delta = now - t->start;
        return delta;
    }
}

/*
 * Set delays to watch
 */
void mbedtls_timing_set_delay(void* data, uint32_t int_ms, uint32_t fin_ms)
{
    mbedtls_timing_delay_context* ctx = (mbedtls_timing_delay_context*)data;

    ctx->int_ms = int_ms;
    ctx->fin_ms = fin_ms;

    if (fin_ms != 0) {
        (void)mbedtls_timing_get_timer(&ctx->timer, 1);
    }
}

/*
 * Get number of delays expired
 */
int mbedtls_timing_get_delay(void* data)
{
    mbedtls_timing_delay_context* ctx = (mbedtls_timing_delay_context*)data;
    unsigned long                 elapsed_ms;

    if (ctx->fin_ms == 0) {
        return -1;
    }

    elapsed_ms = mbedtls_timing_get_timer(&ctx->timer, 0);

    if (elapsed_ms >= ctx->fin_ms) {
        return 2;
    }

    if (elapsed_ms >= ctx->int_ms) {
        return 1;
    }

    return 0;
}

/*
 * Get the final delay.
 */
uint32_t mbedtls_timing_get_final_delay(const mbedtls_timing_delay_context* data) { return data->fin_ms; }

/** timer alarm **************************************************************/
volatile int mbedtls_timing_alarmed = 0;

static drv_soft_timer_inst_t* timer_inst = NULL;

static void timer_irq(void* args)
{
    (void)args;
    mbedtls_timing_alarmed = 1;
    /* One-shot: stop the timer so it does not keep firing. */
    if (timer_inst != NULL) {
        drv_soft_timer_stop(timer_inst);
    }
}

void mbedtls_set_alarm(int seconds)
{
    /* Cancel any previous pending alarm first. */
    if (timer_inst != NULL && drv_soft_timer_is_started(timer_inst)) {
        drv_soft_timer_stop(timer_inst);
    }

    mbedtls_timing_alarmed = 0;

    if (seconds == 0) {
        /* alarm(0) cancels any pending alarm and raises the flag
           immediately so that polling loops terminate at once. */
        mbedtls_timing_alarmed = 1;
        return;
    }

    if (NULL == timer_inst) {
        if (0x00 != drv_soft_timer_create(&timer_inst)) {
            mbedtls_printf("create timer failed.\n");
            mbedtls_timing_alarmed = 1;   /* unblock caller */
            return;
        }
    }

    drv_soft_timer_set_mode(timer_inst, HWTIMER_MODE_ONESHOT);
    drv_soft_timer_set_period(timer_inst, seconds * 1000);

    drv_soft_timer_register_irq(timer_inst, timer_irq, NULL);
    drv_soft_timer_start(timer_inst);
}

unsigned long mbedtls_timing_hardclock(void) { return (unsigned long)utils_cpu_ticks(); }

#endif
