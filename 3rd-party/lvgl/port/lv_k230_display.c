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
#include "lv_k230_display.h"

#include "k_gsdma_comm.h"
#include "k_type.h"
#include "lvgl.h"

#include "hal_utils.h"

#include "mpi_gsdma_api.h"
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    void*              buffer_addr;
    size_t             buffer_size;
    size_t             data_size; // lvgl real used data size
    k_vb_blk_handle    block_handle;
    k_video_frame_info vf_info;
} lv_k230_display_buffer_t;

typedef struct {
    k_vo_layer_attr osd_layer_attr;

    lv_color_format_t color_format;

    int                      buffer_count; /* buffer for lvgl count */
    k_u32                    buffer_pool_id; /* buffer pool id */
    lv_k230_display_buffer_t buffer[2]; /* for lvgl use */

    lv_display_t* lv_disp;
} lv_k230_display_intstance_t;

static k_u8 lv_k230_map_color_format_to_pixel_size(lv_color_format_t color_format)
{
    switch (color_format) {
    case LV_COLOR_FORMAT_L8:
        return 1;
    case LV_COLOR_FORMAT_AL88:
    case LV_COLOR_FORMAT_RGB565:
        return 2;
    case LV_COLOR_FORMAT_RGB888:
    case LV_COLOR_FORMAT_RGB565A8:
        return 3;
    case LV_COLOR_FORMAT_ARGB8888:
    case LV_COLOR_FORMAT_XRGB8888:
        return 4;
    default:
        printf("%s Unsupported color format %d\n", __func__, color_format);

        return 0;
    }
}

static k_pixel_format lv_k230_map_color_format_to_pixel_format(lv_color_format_t color_format)
{
    switch (color_format) {
    case LV_COLOR_FORMAT_L8:
        return PIXEL_FORMAT_RGB_MONOCHROME_8BPP;
    case LV_COLOR_FORMAT_RGB565:
        return PIXEL_FORMAT_RGB_565_LE;
    case LV_COLOR_FORMAT_RGB888:
        return PIXEL_FORMAT_BGR_888;
    case LV_COLOR_FORMAT_ARGB8888:
    case LV_COLOR_FORMAT_XRGB8888:
        return PIXEL_FORMAT_ARGB_8888;
    default:
        printf("%s Unsupported color format %d\n", __func__, color_format);

        return PIXEL_FORMAT_BUTT;
    }
}

static k_u8 lv_k230_osd_layer_pixel_fmt_bpp(k_pixel_format pixel_fmt)
{
    switch (pixel_fmt) {
    case PIXEL_FORMAT_RGB_MONOCHROME_8BPP:
        return 1;
    case PIXEL_FORMAT_RGB_565:
    case PIXEL_FORMAT_RGB_565_LE:
        return 2;
    case PIXEL_FORMAT_RGB_888:
    case PIXEL_FORMAT_BGR_888:
        return 3;
    case PIXEL_FORMAT_ARGB_8888:
    case PIXEL_FORMAT_BGRA_8888:
        return 4;
    default:
        printf("%s Unsupported pixel format %d\n", __func__, pixel_fmt);
        return 0;
    }
}

/* Helper function to allocate and map a single buffer */
static int k230_display_allocate_single_buffer(lv_k230_display_intstance_t* inst, lv_k230_display_buffer_t* buffer,
                                               size_t buffer_size)
{
    buffer->block_handle = kd_mpi_vb_get_block(inst->buffer_pool_id, buffer_size, NULL);
    if (buffer->block_handle == VB_INVALID_HANDLE) {
        printf("Get VB block failed\n");
        return -1;
    }

    buffer->buffer_size                  = buffer_size;
    buffer->vf_info.v_frame.phys_addr[0] = kd_mpi_vb_handle_to_phyaddr(buffer->block_handle);
    buffer->buffer_addr                  = kd_mpi_sys_mmap_cached(buffer->vf_info.v_frame.phys_addr[0], buffer_size);

    if (!buffer->buffer_addr) {
        printf("Mmap failed\n");
        kd_mpi_vb_release_block(buffer->block_handle);
        buffer->block_handle = VB_INVALID_HANDLE;
        return -1;
    }

    return 0;
}

/* Helper function to setup frame information for a buffer */
static void k230_display_setup_frame_info(lv_k230_display_intstance_t* inst, lv_k230_display_buffer_t* buffer)
{
    k_u32 layer_width  = inst->osd_layer_attr.img_size.width;
    k_u32 layer_height = inst->osd_layer_attr.img_size.height;

    k_pixel_format pixel_fmt = inst->osd_layer_attr.pixel_format;

    k_u8 pixel_bpp = lv_k230_osd_layer_pixel_fmt_bpp(pixel_fmt);

    buffer->vf_info.mod_id               = K_ID_VO;
    buffer->vf_info.pool_id              = inst->buffer_pool_id;
    buffer->vf_info.v_frame.width        = layer_width;
    buffer->vf_info.v_frame.height       = layer_height;
    buffer->vf_info.v_frame.stride[0]    = layer_width * pixel_bpp;
    buffer->vf_info.v_frame.pixel_format = pixel_fmt;
    buffer->data_size                    = layer_width * layer_height * pixel_bpp;
}

/* The rest of the functions remain the same... */
static void k230_display_buffer_deinit(lv_k230_display_intstance_t* inst)
{
    int i;

    if (!inst) {
        return;
    }

    // Release LVGL buffers
    for (i = 0; i < inst->buffer_count; i++) {
        if (inst->buffer[i].buffer_addr) {
            kd_mpi_sys_munmap(inst->buffer[i].buffer_addr, inst->buffer[i].buffer_size);
            inst->buffer[i].buffer_addr = NULL;
        }
        if (inst->buffer[i].block_handle != VB_INVALID_HANDLE) {
            kd_mpi_vb_release_block(inst->buffer[i].block_handle);
            inst->buffer[i].block_handle = VB_INVALID_HANDLE;
        }
    }

    // Destroy the VB pool we created internally
    if (inst->buffer_pool_id != VB_INVALID_POOLID) {
        k_s32 ret = kd_mpi_vb_destory_pool(inst->buffer_pool_id);
        if (ret != K_SUCCESS) {
            printf("Failed to destroy VB pool %d, ret: 0x%x\n", inst->buffer_pool_id, ret);
        } else {
            // printf("Successfully destroyed VB pool %d\n", inst->buffer_pool_id);
        }
        inst->buffer_pool_id = VB_INVALID_POOLID;
    }
}

/* Unified buffer configuration function */
static int k230_display_configure_buffers(lv_k230_display_intstance_t* inst)
{
    int   i;
    k_s32 ret;

    size_t buffer_size;

    k_u8  pixel_fmt_bpp;
    k_u32 layer_width, layer_height;

    k_gdma_rotation_e layer_rotate;

    lv_display_render_mode_t render_mode = LV_DISPLAY_RENDER_MODE_DIRECT;

    if (!inst) {
        return -1;
    }

    layer_width  = inst->osd_layer_attr.img_size.width;
    layer_height = inst->osd_layer_attr.img_size.height;
    layer_rotate = inst->osd_layer_attr.func;

    pixel_fmt_bpp = lv_k230_map_color_format_to_pixel_size(LV_COLOR_FORMAT_NATIVE);

    // Calculate buffer size and stride
    buffer_size = layer_width * layer_height * pixel_fmt_bpp;
    if (buffer_size == 0) {
        return -1;
    }

    // Destroy old VB pool if it exists
    if (inst->buffer_pool_id != VB_INVALID_POOLID) {
        k230_display_buffer_deinit(inst);
    }

    // Create VB pool for display buffers
    inst->buffer_pool_id = kd_mpi_vb_create_pool_ex(buffer_size, inst->buffer_count, VB_REMAP_MODE_CACHED);
    if (inst->buffer_pool_id == VB_INVALID_POOLID) {
        printf("Failed to create VB pool for display\n");
        return -1;
    }

    // printf("Created VB pool %d with %d blocks of size %zu\n", inst->buffer_pool_id, inst->buffer_count, buffer_size);

    // Allocate buffers for LVGL
    for (i = 0; i < inst->buffer_count; i++) {
        if (k230_display_allocate_single_buffer(inst, &inst->buffer[i], buffer_size) != 0) {
            printf("Failed to allocate buffer %d\n", i);
            // Clean up previously allocated buffers
            for (int j = 0; j < i; j++) {
                kd_mpi_sys_munmap(inst->buffer[j].buffer_addr, buffer_size);
                kd_mpi_vb_release_block(inst->buffer[j].block_handle);
            }
            kd_mpi_vb_destory_pool(inst->buffer_pool_id);
            inst->buffer_pool_id = VB_INVALID_POOLID;
            return -1;
        }

        k230_display_setup_frame_info(inst, &inst->buffer[i]);
    }

    // Update LVGL display buffers
    if ((GDMA_ROTATE_DEGREE_0 == layer_rotate) || (GDMA_ROTATE_DEGREE_180 == layer_rotate)) {
        render_mode = LV_DISPLAY_RENDER_MODE_DIRECT;
    } else if ((GDMA_ROTATE_DEGREE_90 == layer_rotate) || (GDMA_ROTATE_DEGREE_270 == layer_rotate)) {
        render_mode = LV_DISPLAY_RENDER_MODE_FULL;
    }

    lv_display_set_buffers(inst->lv_disp, inst->buffer[0].buffer_addr, inst->buffer[1].buffer_addr, inst->buffer[0].buffer_size,
                           render_mode);

    // printf("Successfully configured buffers\n");

    return 0;
}

static k_s32 k230_display_configure_osd(lv_k230_display_intstance_t* inst)
{
    if (!inst) {
        return -1;
    }

    k_vo_layer_id layer_id = inst->osd_layer_attr.layer_id;

    if (0x00 != kd_display_layer_disable(layer_id)) {
        printf("disable layer failed\n");
        return -1;
    }

    if (0x00 != kd_display_layer_set_attr(layer_id, &inst->osd_layer_attr)) {
        printf("set layer attr failed\n");
        return -1;
    }

    if (0x00 != kd_display_layer_enable(layer_id)) {
        printf("enable layer failed\n");
        return -1;
    }

    return 0;
}

static uint32_t tick_get_cb(void) { return (uint32_t)utils_cpu_ticks_ms(); }

static inline lv_k230_display_buffer_t* find_buffer_by_color_p(lv_k230_display_intstance_t* inst, uint8_t* color_p)
{
    for (int i = 0; i < inst->buffer_count; i++) {
        lv_k230_display_buffer_t* buff = &inst->buffer[i];

        if (buff->buffer_addr == color_p) {
            return buff;
        }
    }

    return NULL;
}

static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* color_p)
{
    lv_k230_display_intstance_t* inst = lv_display_get_driver_data(disp);

    if (!inst) {
        lv_display_flush_ready(disp);
        return;
    }

    // Get current buffers
    lv_k230_display_buffer_t* current_buffer = find_buffer_by_color_p(inst, color_p);

    if (!current_buffer) {
        printf("Invalid source buffer\n");
        lv_display_flush_ready(disp);
        return;
    }

    kd_mpi_sys_mmz_flush_cache(current_buffer->vf_info.v_frame.phys_addr[0], current_buffer->buffer_addr,
                               current_buffer->data_size);

    kd_display_layer_push_frame(inst->osd_layer_attr.layer_id, &current_buffer->vf_info);

    lv_display_flush_ready(disp);
}

static void event_cb(lv_event_t* e)
{
    lv_event_code_t              code    = lv_event_get_code(e);
    lv_display_t*                display = (lv_display_t*)lv_event_get_target(e);
    lv_k230_display_intstance_t* inst    = lv_display_get_driver_data(display);

    switch (code) {
    case LV_EVENT_DELETE:
        if (inst) {
            // Disable OSD layer
            kd_display_layer_disable(inst->osd_layer_attr.layer_id);

            // Clean up buffers
            k230_display_buffer_deinit(inst);

            lv_display_set_driver_data(display, NULL);
            lv_free(inst);
        }
        break;
    case LV_EVENT_RESOLUTION_CHANGED:
        if (inst) {
            k_vo_layer_id layer_id = inst->osd_layer_attr.layer_id;

            k_u32 layer_width  = inst->osd_layer_attr.img_size.width;
            k_u32 layer_height = inst->osd_layer_attr.img_size.height;

            int32_t new_width  = lv_display_get_horizontal_resolution(display);
            int32_t new_height = lv_display_get_vertical_resolution(display);

            k_gdma_rotation_e new_rotate   = GDMA_ROTATE_NONE;
            k_gdma_rotation_e layer_rotate = inst->osd_layer_attr.func;

            lv_display_rotation_t lv_rot = lv_display_get_rotation(inst->lv_disp);

            int32_t panel_width, panel_height;

            if (0x00 != kd_display_get_resolution((k_u32*)&panel_width, (k_u32*)&panel_height)) {
                printf("get disp resolution failed, skip update resolution\n");

                break;
            }

            if ((panel_width == new_width) && (panel_height == new_height)) {
                new_rotate = GDMA_ROTATE_NONE;

                if (LV_DISPLAY_ROTATION_180 == lv_rot) {
                    new_rotate = GDMA_ROTATE_DEGREE_180;
                }
            } else if ((panel_width == new_height) && (panel_height == new_width)) {
                if (LV_DISPLAY_ROTATION_90 == lv_rot) {
                    new_rotate = GDMA_ROTATE_DEGREE_270;
                } else if (LV_DISPLAY_ROTATION_270 == lv_rot) {
                    new_rotate = GDMA_ROTATE_DEGREE_90;
                } else {
                    printf("unsupport lvgl rotation\n");

                    break;
                }
            } else {
                printf("unsupport new resolution %dx%d vs %dx%d\n", new_width, new_height, panel_width, panel_height);

                break;
            }

            printf("Resolution changed: %dx%d, rot %d\n", new_width, new_height, new_rotate);

            if ((layer_width != new_width) || (layer_height != new_height) || (new_rotate != layer_rotate)) {
                inst->osd_layer_attr.img_size.width  = new_width;
                inst->osd_layer_attr.img_size.height = new_height;
                inst->osd_layer_attr.func            = new_rotate;

                if (k230_display_configure_buffers(inst) != 0) {
                    printf("Failed to reconfigure buffers for new resolution\n");
                    break;
                }

                if (k230_display_configure_osd(inst) != 0) {
                    printf("Failed to reconfigure OSD for new resolution\n");
                    break;
                }
            }
        }
        break;
    case LV_EVENT_COLOR_FORMAT_CHANGED:
        if (inst) {
            k_vo_layer_id layer_id = inst->osd_layer_attr.layer_id;

            // Store old format for rollback
            lv_color_format_t old_color_format = inst->color_format;
            k_pixel_format    old_pixel_format = inst->osd_layer_attr.pixel_format;

            lv_color_format_t new_color_format = lv_display_get_color_format(display);

            // Check if the new format is supported
            k_pixel_format new_pixel_format = lv_k230_map_color_format_to_pixel_format(new_color_format);
            if (PIXEL_FORMAT_BUTT == new_pixel_format) {
                printf("Unsupported color format: %d\n", new_color_format);
                // Revert to old format
                lv_display_set_color_format(display, old_color_format);
                break;
            }

            // If format is the same, no need to reconfigure
            if ((new_color_format == inst->color_format) || (new_pixel_format == old_pixel_format)) {
                printf("Color format unchanged, skipping reconfiguration\n");
                break;
            }

            printf("Reconfiguring display for new color format %d...\n", new_color_format);

            // Update instance with new format temporarily
            inst->color_format                = new_color_format;
            inst->osd_layer_attr.pixel_format = new_pixel_format;

            // Reconfigure buffers for new color format
            if (k230_display_configure_buffers(inst) != 0) {
                printf("Failed to reconfigure buffers for new color format\n");
                // Rollback to old format
                inst->color_format                = old_color_format;
                inst->osd_layer_attr.pixel_format = old_pixel_format;

                lv_display_set_color_format(display, old_color_format);
                break;
            }

            // Reconfigure OSD for new color format
            if (kd_display_layer_update_pixel_format(layer_id, new_pixel_format) != 0) {
                printf("Failed to reconfigure OSD for new color format\n");
                // Rollback: reconfigure buffers back to old format
                inst->color_format                = old_color_format;
                inst->osd_layer_attr.pixel_format = old_pixel_format;

                lv_display_set_color_format(display, old_color_format);
                break;
            }

            // Successfully updated color format
            // printf("Successfully changed color format from %d to %d\n", old_color_format, new_color_format);
        }
        break;
    default:
        return;
    }
}

lv_display_t* lv_k230_display_create(k_vo_layer_id layer, uint8_t alpha)
{
    k_s32 ret;

    k_pixel_format osd_layer_pixel_fmt;

    k_u32 panel_width, panel_height;

    if ((K_VO_LAYER_OSD0 > layer) || (K_MAX_VO_LAYER_NR <= layer)) {
        printf("invalid layer, only support osd layers\n");

        goto _invalid_osd_layer;
    }

    if (0x00 != kd_display_get_resolution(&panel_width, &panel_height)) {
        printf("get panel resolution failed\n");

        goto _failed_get_resolution;
    }

    lv_k230_display_intstance_t* inst = lv_malloc_zeroed(sizeof(lv_k230_display_intstance_t));
    lv_display_t*                disp = lv_display_create(panel_width, panel_height);
    LV_ASSERT_MALLOC(inst || disp);
    if ((NULL == inst) || (NULL == disp)) {
        goto _failed_malloc;
    }

    inst->buffer_count   = 2;
    inst->buffer_pool_id = VB_INVALID_POOLID; /* Will be created internally */

    /* Initialize rotation state */
    inst->lv_disp = disp;

    inst->color_format  = lv_display_get_color_format(disp);
    osd_layer_pixel_fmt = lv_k230_map_color_format_to_pixel_format(inst->color_format);

    if (PIXEL_FORMAT_BUTT == osd_layer_pixel_fmt) {
        printf("Unsupported color format\n");
        goto _failed_map_color_format;
    }

    memset(&inst->osd_layer_attr, 0, sizeof(k_vo_layer_attr));

    inst->osd_layer_attr.layer_id        = layer;
    inst->osd_layer_attr.position.x      = 0;
    inst->osd_layer_attr.position.y      = 0;
    inst->osd_layer_attr.img_size.width  = panel_width;
    inst->osd_layer_attr.img_size.height = panel_height;
    inst->osd_layer_attr.pixel_format    = osd_layer_pixel_fmt;
    inst->osd_layer_attr.global_alpha    = alpha;
    inst->osd_layer_attr.func            = 0; // default no rotate.
    inst->osd_layer_attr.rot_buf_nr      = 2;
    inst->osd_layer_attr.rot_buf_bpp     = 4; // we need support user dynamic update pixelformat

    if (0x00 != k230_display_configure_buffers(inst)) {
        printf("Buffer init failed\n");
        goto _failed_buffer_init;
    }

    if (0x00 != k230_display_configure_osd(inst)) {
        printf("OSD init failed\n");
        goto _failed_osd_init;
    }

    lv_display_set_driver_data(disp, inst);

    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, inst->buffer[0].buffer_addr, inst->buffer[1].buffer_addr, inst->buffer[0].buffer_size,
                           LV_DISPLAY_RENDER_MODE_DIRECT);

    lv_display_add_event_cb(disp, event_cb, LV_EVENT_RESOLUTION_CHANGED, NULL);
    lv_display_add_event_cb(disp, event_cb, LV_EVENT_COLOR_FORMAT_CHANGED, NULL);
    lv_display_add_event_cb(disp, event_cb, LV_EVENT_DELETE, NULL);

    lv_tick_set_cb(tick_get_cb);

    return disp;

_failed_osd_init:
_failed_dma_init:
    k230_display_buffer_deinit(inst);
_failed_buffer_init:
_failed_map_color_format:
    if (inst) {
        lv_free(inst);
    }
_failed_malloc:
_invalid_resolution:
_failed_get_resolution:
_invalid_osd_layer:

    return NULL;
}
