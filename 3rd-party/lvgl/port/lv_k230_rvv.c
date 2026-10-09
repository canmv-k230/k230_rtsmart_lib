/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "lv_k230_rvv.h"

#include "lv_k230_vglite.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RVV_MIN_ROW_PIXELS 8

static bool rvv_can_handle(const lv_draw_sw_blend_fill_dsc_t * dsc,
                           uint32_t dest_px_size)
{
    return lv_k230_renderer_rvv_enabled() && dsc && dsc->dest_buf &&
           dsc->dest_w >= RVV_MIN_ROW_PIXELS && dsc->dest_h > 0 &&
           (dest_px_size == 3u || dest_px_size == 4u);
}

static void rvv_fill_row(uint8_t * dest, size_t count, uint32_t color,
                         uint32_t dest_px_size)
{
    while(count != 0u) {
        size_t vl;
        if(dest_px_size == 4u && ((uintptr_t)dest & 3u) == 0u) {
            asm volatile(
                "vsetvli %[vl], %[count], e32, m8, ta, ma\n\t"
                "vmv.v.x v0, %[color]\n\t"
                "vse32.v v0, (%[dest])\n\t"
                : [vl] "=&r"(vl)
                : [count] "r"(count), [dest] "r"(dest), [color] "r"(color)
                : "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "memory");
        }
        else if(dest_px_size == 4u) {
            const uint32_t blue = color & 0xffu;
            const uint32_t green = (color >> 8) & 0xffu;
            const uint32_t red = (color >> 16) & 0xffu;
            const uint32_t alpha = color >> 24;
            asm volatile(
                "vsetvli %[vl], %[count], e8, m2, ta, ma\n\t"
                "vmv.v.x v0, %[blue]\n\t"
                "vmv.v.x v2, %[green]\n\t"
                "vmv.v.x v4, %[red]\n\t"
                "vmv.v.x v6, %[alpha]\n\t"
                "vsseg4e8.v v0, (%[dest])\n\t"
                : [vl] "=&r"(vl)
                : [count] "r"(count), [dest] "r"(dest),
                  [blue] "r"(blue), [green] "r"(green), [red] "r"(red),
                  [alpha] "r"(alpha)
                : "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "memory");
        }
        else {
            const uint32_t blue = color & 0xffu;
            const uint32_t green = (color >> 8) & 0xffu;
            const uint32_t red = (color >> 16) & 0xffu;
            asm volatile(
                "vsetvli %[vl], %[count], e8, m2, ta, ma\n\t"
                "vmv.v.x v0, %[blue]\n\t"
                "vmv.v.x v2, %[green]\n\t"
                "vmv.v.x v4, %[red]\n\t"
                "vsseg3e8.v v0, (%[dest])\n\t"
                : [vl] "=&r"(vl)
                : [count] "r"(count), [dest] "r"(dest),
                  [blue] "r"(blue), [green] "r"(green), [red] "r"(red)
                : "v0", "v1", "v2", "v3", "v4", "v5", "memory");
        }

        dest += vl * dest_px_size;
        count -= vl;
    }
}

static void rvv_mix_color_row(uint8_t * dest, size_t count, uint32_t dest_px_size,
                              lv_color_t color, const uint8_t * mask,
                              uint32_t opacity, bool combine_mask)
{
    const uintptr_t four = 4u;
    const uintptr_t threshold = LV_OPA_MAX - 1u;

    while(count != 0u) {
        size_t vl;
        asm volatile(
            "vsetvli %[vl], %[count], e8, m2, ta, ma\n\t"
            "beq %[px_size], %[four], 1f\n\t"
            "vlseg3e8.v v8, (%[dest])\n\t"
            "j 2f\n\t"
            "1:\n\t"
            "vlseg4e8.v v8, (%[dest])\n\t"
            "vmv.v.v v30, v14\n\t"
            "2:\n\t"
            "beqz %[mask], 3f\n\t"
            "vle8.v v16, (%[mask])\n\t"
            "beqz %[combine], 4f\n\t"
            "vwmulu.vx v20, v16, %[opacity]\n\t"
            "vnsrl.wi v16, v20, 8\n\t"
            "j 4f\n\t"
            "3:\n\t"
            "vmv.v.x v16, %[opacity]\n\t"
            "4:\n\t"
            "vrsub.vx v18, v16, %[full]\n\t"

            /* Blue */
            "vwmulu.vx v20, v16, %[blue]\n\t"
            "vwmaccu.vv v20, v8, v18\n\t"
            "vnsrl.wi v24, v20, 8\n\t"
            "vmseq.vi v0, v16, 0\n\t"
            "vmerge.vvm v24, v24, v8, v0\n\t"
            "vmsgtu.vx v0, v16, %[threshold]\n\t"
            "vmerge.vxm v24, v24, %[blue], v0\n\t"

            /* Green */
            "vwmulu.vx v20, v16, %[green]\n\t"
            "vwmaccu.vv v20, v10, v18\n\t"
            "vnsrl.wi v26, v20, 8\n\t"
            "vmseq.vi v0, v16, 0\n\t"
            "vmerge.vvm v26, v26, v10, v0\n\t"
            "vmsgtu.vx v0, v16, %[threshold]\n\t"
            "vmerge.vxm v26, v26, %[green], v0\n\t"

            /* Red */
            "vwmulu.vx v20, v16, %[red]\n\t"
            "vwmaccu.vv v20, v12, v18\n\t"
            "vnsrl.wi v28, v20, 8\n\t"
            "vmseq.vi v0, v16, 0\n\t"
            "vmerge.vvm v28, v28, v12, v0\n\t"
            "vmsgtu.vx v0, v16, %[threshold]\n\t"
            "vmerge.vxm v28, v28, %[red], v0\n\t"

            "beq %[px_size], %[four], 5f\n\t"
            "vsseg3e8.v v24, (%[dest])\n\t"
            "j 6f\n\t"
            "5:\n\t"
            "vsseg4e8.v v24, (%[dest])\n\t"
            "6:\n\t"
            : [vl] "=&r"(vl)
            : [count] "r"(count), [dest] "r"(dest), [px_size] "r"((uintptr_t)dest_px_size),
              [four] "r"(four), [mask] "r"(mask), [combine] "r"((uintptr_t)combine_mask),
              [opacity] "r"((uintptr_t)opacity), [full] "r"((uintptr_t)255u),
              [threshold] "r"(threshold), [blue] "r"((uintptr_t)color.blue),
              [green] "r"((uintptr_t)color.green), [red] "r"((uintptr_t)color.red)
            : "v0", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
              "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
              "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31", "memory");

        dest += vl * dest_px_size;
        if(mask) {
            mask += vl;
        }
        count -= vl;
    }
}

static lv_result_t rvv_color_to_rgb888(lv_draw_sw_blend_fill_dsc_t * dsc,
                                       uint32_t dest_px_size,
                                       const uint8_t * mask, bool combine_mask)
{
    if(!rvv_can_handle(dsc, dest_px_size)) {
        return LV_RESULT_INVALID;
    }

    uint8_t * dest = dsc->dest_buf;
    const uint8_t * mask_row = mask;
    for(int32_t y = 0; y < dsc->dest_h; y++) {
        rvv_mix_color_row(dest, (size_t)dsc->dest_w, dest_px_size, dsc->color,
                          mask_row, dsc->opa, combine_mask);
        dest += dsc->dest_stride;
        if(mask_row) {
            mask_row += dsc->mask_stride;
        }
    }

    lv_k230_renderer_note_rvv((uint64_t)(uint32_t)dsc->dest_w * (uint32_t)dsc->dest_h);
    return LV_RESULT_OK;
}

lv_result_t lv_k230_rvv_color_to_rgb888(lv_draw_sw_blend_fill_dsc_t * dsc,
                                         uint32_t dest_px_size)
{
    if(!rvv_can_handle(dsc, dest_px_size)) {
        return LV_RESULT_INVALID;
    }

    uint32_t color = lv_color_to_u32(dsc->color);
    uint8_t * dest = dsc->dest_buf;
    for(int32_t y = 0; y < dsc->dest_h; y++) {
        rvv_fill_row(dest, (size_t)dsc->dest_w, color, dest_px_size);
        dest += dsc->dest_stride;
    }

    lv_k230_renderer_note_rvv((uint64_t)(uint32_t)dsc->dest_w * (uint32_t)dsc->dest_h);
    return LV_RESULT_OK;
}

lv_result_t lv_k230_rvv_color_to_rgb888_with_opa(lv_draw_sw_blend_fill_dsc_t * dsc,
                                                  uint32_t dest_px_size)
{
    return rvv_color_to_rgb888(dsc, dest_px_size, NULL, false);
}

lv_result_t lv_k230_rvv_color_to_rgb888_with_mask(lv_draw_sw_blend_fill_dsc_t * dsc,
                                                   uint32_t dest_px_size)
{
    return rvv_color_to_rgb888(dsc, dest_px_size, dsc->mask_buf, false);
}

lv_result_t lv_k230_rvv_color_to_rgb888_with_opa_mask(lv_draw_sw_blend_fill_dsc_t * dsc,
                                                       uint32_t dest_px_size)
{
    return rvv_color_to_rgb888(dsc, dest_px_size, dsc->mask_buf, true);
}

lv_result_t lv_k230_rvv_color_to_argb8888(lv_draw_sw_blend_fill_dsc_t * dsc)
{
    if(!rvv_can_handle(dsc, 4u)) {
        return LV_RESULT_INVALID;
    }

    uint32_t color = lv_color_to_u32(dsc->color);
    uint8_t * dest = dsc->dest_buf;
    for(int32_t y = 0; y < dsc->dest_h; y++) {
        rvv_fill_row(dest, (size_t)dsc->dest_w, color, 4u);
        dest += dsc->dest_stride;
    }

    lv_k230_renderer_note_rvv((uint64_t)(uint32_t)dsc->dest_w * (uint32_t)dsc->dest_h);
    return LV_RESULT_OK;
}

static void reference_mix(uint8_t * dest, lv_color_t color, uint8_t mix)
{
    if(mix == 0u) {
        return;
    }
    if(mix >= LV_OPA_MAX) {
        dest[0] = color.blue;
        dest[1] = color.green;
        dest[2] = color.red;
        return;
    }

    uint32_t inverse = 255u - mix;
    dest[0] = (uint8_t)(((uint32_t)color.blue * mix + dest[0] * inverse) >> 8);
    dest[1] = (uint8_t)(((uint32_t)color.green * mix + dest[1] * inverse) >> 8);
    dest[2] = (uint8_t)(((uint32_t)color.red * mix + dest[2] * inverse) >> 8);
}

bool lv_k230_renderer_rvv_self_test(void)
{
    enum { WIDTH = 37, HEIGHT = 3, MAX_STRIDE = WIDTH * 4 + 13 };
    uint8_t actual[MAX_STRIDE * HEIGHT];
    uint8_t expected[MAX_STRIDE * HEIGHT];
    uint8_t masks[(WIDTH + 5) * HEIGHT];
    const lv_color_t color = {.blue = 0x29, .green = 0x83, .red = 0xd7};

    if(!lv_k230_renderer_rvv_enabled()) {
        return false;
    }

    for(size_t i = 0; i < sizeof(masks); i++) {
        masks[i] = (uint8_t)((i * 47u + 3u) & 0xffu);
    }

    for(uint32_t px_size = 3u; px_size <= 4u; px_size++) {
        int32_t stride = WIDTH * (int32_t)px_size + 13;
        for(uint32_t operation = 0; operation < 4u; operation++) {
            for(size_t i = 0; i < sizeof(actual); i++) {
                actual[i] = (uint8_t)((i * 29u + operation * 11u + px_size) & 0xffu);
            }
            memcpy(expected, actual, sizeof(actual));

            lv_draw_sw_blend_fill_dsc_t dsc = {
                .dest_buf = actual,
                .dest_w = WIDTH,
                .dest_h = HEIGHT,
                .dest_stride = stride,
                .mask_buf = masks,
                .mask_stride = WIDTH + 5,
                .color = color,
                .opa = operation == 1u ? 137u : operation == 3u ? 173u : 255u,
            };

            lv_result_t result;
            if(operation == 0u) {
                result = lv_k230_rvv_color_to_rgb888(&dsc, px_size);
            }
            else if(operation == 1u) {
                result = lv_k230_rvv_color_to_rgb888_with_opa(&dsc, px_size);
            }
            else if(operation == 2u) {
                result = lv_k230_rvv_color_to_rgb888_with_mask(&dsc, px_size);
            }
            else {
                result = lv_k230_rvv_color_to_rgb888_with_opa_mask(&dsc, px_size);
            }
            if(result != LV_RESULT_OK) {
                return false;
            }

            for(int32_t y = 0; y < HEIGHT; y++) {
                for(int32_t x = 0; x < WIDTH; x++) {
                    uint8_t * pixel = expected + y * stride + x * (int32_t)px_size;
                    if(operation == 0u) {
                        pixel[0] = color.blue;
                        pixel[1] = color.green;
                        pixel[2] = color.red;
                        if(px_size == 4u) {
                            pixel[3] = 0xffu;
                        }
                    }
                    else {
                        uint8_t mix = dsc.opa;
                        if(operation >= 2u) {
                            mix = masks[y * dsc.mask_stride + x];
                            if(operation == 3u) {
                                mix = (uint8_t)(((uint32_t)mix * dsc.opa) >> 8);
                            }
                        }
                        reference_mix(pixel, color, mix);
                    }
                }
            }

            if(memcmp(actual, expected, sizeof(actual)) != 0) {
                return false;
            }
        }
    }

    return true;
}
