/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Install K230 accelerated draw-buffer copy and color-conversion handlers. */
void lv_k230_image_convert_init(void);

/** Release lazy hardware-conversion resources. */
void lv_k230_image_convert_deinit(void);

/** Return the backend used by the most recent color conversion. */
const char * lv_k230_image_convert_backend(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif
