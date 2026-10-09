/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#pragma once

#include "src/draw/sw/blend/lv_draw_sw_blend_private.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_result_t lv_k230_rvv_color_to_rgb888(lv_draw_sw_blend_fill_dsc_t * dsc,
                                         uint32_t dest_px_size);
lv_result_t lv_k230_rvv_color_to_rgb888_with_opa(lv_draw_sw_blend_fill_dsc_t * dsc,
                                                  uint32_t dest_px_size);
lv_result_t lv_k230_rvv_color_to_rgb888_with_mask(lv_draw_sw_blend_fill_dsc_t * dsc,
                                                   uint32_t dest_px_size);
lv_result_t lv_k230_rvv_color_to_rgb888_with_opa_mask(lv_draw_sw_blend_fill_dsc_t * dsc,
                                                       uint32_t dest_px_size);
lv_result_t lv_k230_rvv_color_to_argb8888(lv_draw_sw_blend_fill_dsc_t * dsc);
#define LV_DRAW_SW_COLOR_BLEND_TO_RGB888(dsc, px_size) \
    lv_k230_rvv_color_to_rgb888((dsc), (px_size))
#define LV_DRAW_SW_COLOR_BLEND_TO_RGB888_WITH_OPA(dsc, px_size) \
    lv_k230_rvv_color_to_rgb888_with_opa((dsc), (px_size))
#define LV_DRAW_SW_COLOR_BLEND_TO_RGB888_WITH_MASK(dsc, px_size) \
    lv_k230_rvv_color_to_rgb888_with_mask((dsc), (px_size))
#define LV_DRAW_SW_COLOR_BLEND_TO_RGB888_MIX_MASK_OPA(dsc, px_size) \
    lv_k230_rvv_color_to_rgb888_with_opa_mask((dsc), (px_size))
#define LV_DRAW_SW_COLOR_BLEND_TO_ARGB8888(dsc) \
    lv_k230_rvv_color_to_argb8888(dsc)

#ifdef __cplusplus
} /* extern "C" */
#endif
