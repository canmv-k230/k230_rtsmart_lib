/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "lv_k230_sysmon.h"

#include "canmv_misc.h"
#include "lvgl.h"

uint32_t lv_k230_sysmon_get_idle_percent(void)
{
    int cpu_usage;

    if (canmv_misc_get_cpu_usage(&cpu_usage) != 0) {
        return lv_timer_get_idle();
    }

    if (cpu_usage < 0) {
        cpu_usage = 0;
    } else if (cpu_usage > 100) {
        cpu_usage = 100;
    }

    return 100U - (uint32_t)cpu_usage;
}
