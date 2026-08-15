/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#include "k_video_comm.h"
#include "k_type.h"

void lv_k230_vglite_register_buffer(void * virt, k_u64 phys, size_t size, bool cached);
void lv_k230_vglite_register_mpp_buffer(void * virt, k_u64 phys, size_t size, bool cached,
                                          k_pixel_format format);
bool lv_k230_vglite_get_mpp_buffer(const void * ptr, size_t size, k_u64 * phys, k_pixel_format * format);
void lv_k230_vglite_unregister_buffer(void * virt);
bool lv_k230_vglite_get_buffer_phys(const void * ptr, size_t size, k_u64 * phys);
bool lv_k230_vglite_wait_idle(void);
const char * lv_k230_vglite_renderer_name(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif
