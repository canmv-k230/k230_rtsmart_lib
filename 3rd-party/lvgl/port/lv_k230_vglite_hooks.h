/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#include <stdbool.h>

#include "vg_lite.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(LV_VG_LITE_GPU_INIT_HAS_RESULT) && LV_VG_LITE_GPU_INIT_HAS_RESULT
bool gpu_init(void);
#else
void gpu_init(void);
#endif
bool lv_vg_lite_port_map_buffer(vg_lite_buffer_t * buffer);

#ifdef __cplusplus
} /*extern "C"*/
#endif
