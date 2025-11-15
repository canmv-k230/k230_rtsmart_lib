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

#include "hal_utils.h"
#include "k_connector_comm.h"
#include "k_dma_comm.h"
#include "k_module.h"
#include "k_sys_comm.h"
#include "k_type.h"
#include "k_vb_comm.h"
#include "k_video_comm.h"
#include "k_vo_comm.h"
#include "lvgl.h"
#include "mpi_connector_api.h"
#include "mpi_dma_api.h"
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"
#include "mpi_vo_api.h"
#include "mpi_vvi_api.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#ifndef DMA_PIXEL_FORMAT_BUTT
#define DMA_PIXEL_FORMAT_BUTT (0xFFFFFFFF)
#endif

typedef struct {
    void*              buffer_addr;
    size_t             buffer_size;
    k_vb_blk_handle    block_handle;
    k_video_frame_info vf_info;
} lv_k230_display_buffer_t;

typedef struct {
    k_vo_osd         osd_layer;
    k_connector_info panel_info;
    k_vo_pub_attr    vo_attr; /* VO attributes */

    int layer_configured;

    lv_color_format_t color_format;
    k_pixel_format    pixel_format;

    int                      buffer_count; /* buffer for lvgl count */
    k_u32                    buffer_pool_id; /* buffer pool id */
    lv_k230_display_buffer_t buffer[2]; /* for lvgl use */
    lv_k230_display_buffer_t buffer_rotate; /* for hardware use,  */

    /* Rotation and resolution support */
    lv_display_t*         lv_disp;
    lv_display_rotation_t lv_rotation;

    /* DMA support for hardware rotation */
    k_s32 dma_chn; /* DMA channel for rotation */
    bool  dma_initialized; /* DMA initialization flag */
} lv_k230_display_intstance_t;

/* Forward declarations */
static int  k230_display_configure_buffers(lv_k230_display_intstance_t* inst);
static void k230_display_buffer_deinit(lv_k230_display_intstance_t* inst);
static int  k230_display_configure_osd(lv_k230_display_intstance_t* inst);

/* Helper functions to eliminate duplication */
static size_t   k230_display_calculate_buffer_size(lv_k230_display_intstance_t* inst);
static uint32_t k230_display_calculate_stride_bytes(lv_k230_display_intstance_t* inst);
static int      k230_display_allocate_single_buffer(lv_k230_display_intstance_t* inst, lv_k230_display_buffer_t* buffer,
                                                    size_t buffer_size);
static void     k230_display_setup_frame_info(lv_k230_display_intstance_t* inst, lv_k230_display_buffer_t* buffer,
                                              uint32_t stride_bytes);
static int      k230_display_configure_osd_attributes(lv_k230_display_intstance_t* inst, k_vo_video_osd_attr* osd_attr);

static k_pixel_format lv_k230_map_color_format_to_pixel_format(lv_color_format_t color_format);

static uint32_t tick_get_cb(void);
static void     flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* color_p);
static void     event_cb(lv_event_t* e);

/* DMA support functions */
static int  k230_display_dma_init(lv_k230_display_intstance_t* inst);
static void k230_display_dma_deinit(lv_k230_display_intstance_t* inst);
static int  k230_display_rotate_using_gdma(lv_k230_display_intstance_t* inst, lv_k230_display_buffer_t* src_buf,
                                           lv_k230_display_buffer_t* dst_buf);

lv_display_t* lv_k230_display_create(k_connector_type connector_type, k_vo_osd layer)
{
    k_connector_info info;
    k_s32            ret;
    int              panel_width, panel_height;

    if (K_SUCCESS != kd_mpi_get_connector_info(connector_type, &info)) {
        printf("get connector info failed\n");
        goto _failed_get_info;
    }

    panel_width  = info.resolution.hdisplay;
    panel_height = info.resolution.vdisplay;

    lv_k230_display_intstance_t* inst = lv_malloc_zeroed(sizeof(lv_k230_display_intstance_t));
    lv_display_t*                disp = lv_display_create(panel_width, panel_height);
    LV_ASSERT_MALLOC(inst || disp);
    if ((NULL == inst) || (NULL == disp)) {
        goto _failed_malloc;
    }

    inst->osd_layer      = layer;
    inst->buffer_count   = 2;
    inst->buffer_pool_id = VB_INVALID_POOLID; /* Will be created internally */
    lv_memcpy(&inst->panel_info, &info, sizeof(inst->panel_info));

    /* Initialize rotation state */
    inst->lv_disp     = disp;
    inst->lv_rotation = LV_DISPLAY_ROTATION_0;

    /* Initialize DMA state */
    inst->dma_chn         = -1;
    inst->dma_initialized = false;

    inst->color_format = lv_display_get_color_format(disp);
    inst->pixel_format = lv_k230_map_color_format_to_pixel_format(inst->color_format);
    if (PIXEL_FORMAT_BUTT == inst->pixel_format) {
        printf("Unsupported color format\n");
        goto _failed_map_color_format;
    }

    if (0x00 != k230_display_configure_buffers(inst)) {
        printf("Buffer init failed\n");
        goto _failed_buffer_init;
    }

    if (0x00 != k230_display_configure_osd(inst)) {
        printf("OSD init failed\n");
        goto _failed_osd_init;
    }

    if (0x00 != k230_display_dma_init(inst)) {
        printf("DMA init failed\n");
        goto _failed_dma_init;
    }

    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, inst->buffer[0].buffer_addr, inst->buffer[1].buffer_addr, inst->buffer[0].buffer_size,
                           LV_DISPLAY_RENDER_MODE_DIRECT);

    lv_display_set_driver_data(disp, inst);
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
_failed_get_info:
_failed_invalid_buffer_count:

    return NULL;
}

/* Helper function to calculate buffer size based on pixel format */
static size_t k230_display_calculate_buffer_size(lv_k230_display_intstance_t* inst)
{
    size_t buffer_size;

    switch (inst->pixel_format) {
    case PIXEL_FORMAT_RGB_565:
        buffer_size = inst->panel_info.resolution.hdisplay * inst->panel_info.resolution.vdisplay * 2;
        break;
    case PIXEL_FORMAT_RGB_888:
        buffer_size = inst->panel_info.resolution.hdisplay * inst->panel_info.resolution.vdisplay * 3;
        break;
    case PIXEL_FORMAT_ARGB_8888:
        buffer_size = inst->panel_info.resolution.hdisplay * inst->panel_info.resolution.vdisplay * 4;
        break;
    default:
        printf("Unsupported pixel format for buffer calculation\n");
        return 0;
    }

    // Add some extra space for alignment
    buffer_size = (buffer_size + 4095) & ~4095; // Align to 4K boundary
    return buffer_size;
}

/* Helper function to calculate stride in bytes */
static uint32_t k230_display_calculate_stride_bytes(lv_k230_display_intstance_t* inst)
{
    uint32_t bytes_per_pixel;

    switch (inst->pixel_format) {
    case PIXEL_FORMAT_RGB_565:
        bytes_per_pixel = 2;
        break;
    case PIXEL_FORMAT_RGB_888:
        bytes_per_pixel = 3;
        break;
    case PIXEL_FORMAT_ARGB_8888:
        bytes_per_pixel = 4;
        break;
    default:
        bytes_per_pixel = 4;
        break;
    }

    return inst->panel_info.resolution.hdisplay * bytes_per_pixel;
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
static void k230_display_setup_frame_info(lv_k230_display_intstance_t* inst, lv_k230_display_buffer_t* buffer,
                                          uint32_t stride_bytes)
{
    buffer->vf_info.mod_id               = K_ID_VO;
    buffer->vf_info.pool_id              = inst->buffer_pool_id;
    buffer->vf_info.v_frame.width        = inst->panel_info.resolution.hdisplay;
    buffer->vf_info.v_frame.height       = inst->panel_info.resolution.vdisplay;
    buffer->vf_info.v_frame.stride[0]    = stride_bytes;
    buffer->vf_info.v_frame.pixel_format = inst->pixel_format;
}

/* Unified buffer configuration function */
static int k230_display_configure_buffers(lv_k230_display_intstance_t* inst)
{
    size_t   buffer_size;
    uint32_t stride_bytes;
    k_s32    ret;
    int      i;

    if (!inst) {
        return -1;
    }

    printf("Configuring buffers with format: %d\n", inst->pixel_format);

    // Calculate buffer size and stride
    buffer_size = k230_display_calculate_buffer_size(inst);
    if (buffer_size == 0) {
        return -1;
    }

    stride_bytes = k230_display_calculate_stride_bytes(inst);

    // Destroy old VB pool if it exists
    if (inst->buffer_pool_id != VB_INVALID_POOLID) {
        k230_display_buffer_deinit(inst);
    }

    // Create VB pool for display buffers
    k_vb_pool_config pool_config;
    memset(&pool_config, 0, sizeof(pool_config));
    pool_config.blk_cnt  = inst->buffer_count + 1;
    pool_config.blk_size = buffer_size;
    pool_config.mode     = VB_REMAP_MODE_CACHED;

    inst->buffer_pool_id = kd_mpi_vb_create_pool(&pool_config);
    if (inst->buffer_pool_id == VB_INVALID_POOLID) {
        printf("Failed to create VB pool for display\n");
        return -1;
    }

    printf("Created VB pool %d with %d blocks of size %zu\n", inst->buffer_pool_id, pool_config.blk_cnt, buffer_size);

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

        k230_display_setup_frame_info(inst, &inst->buffer[i], stride_bytes);
    }

    // Allocate display buffer
    if (k230_display_allocate_single_buffer(inst, &inst->buffer_rotate, buffer_size) != 0) {
        printf("Failed to allocate display buffer\n");
        k230_display_buffer_deinit(inst);
        return -1;
    }
    k230_display_setup_frame_info(inst, &inst->buffer_rotate, stride_bytes);

    // Update LVGL display buffers
    lv_display_set_buffers(inst->lv_disp, inst->buffer[0].buffer_addr, inst->buffer[1].buffer_addr, inst->buffer[0].buffer_size,
                           LV_DISPLAY_RENDER_MODE_FULL);

    printf("Successfully configured buffers\n");
    return 0;
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

    // Release display buffer
    if (inst->buffer_rotate.buffer_addr) {
        kd_mpi_sys_munmap(inst->buffer_rotate.buffer_addr, inst->buffer_rotate.buffer_size);
        inst->buffer_rotate.buffer_addr = NULL;
    }
    if (inst->buffer_rotate.block_handle != VB_INVALID_HANDLE) {
        kd_mpi_vb_release_block(inst->buffer_rotate.block_handle);
        inst->buffer_rotate.block_handle = VB_INVALID_HANDLE;
    }

    // Destroy the VB pool we created internally
    if (inst->buffer_pool_id != VB_INVALID_POOLID) {
        k_s32 ret = kd_mpi_vb_destory_pool(inst->buffer_pool_id);
        if (ret != K_SUCCESS) {
            printf("Failed to destroy VB pool %d, ret: 0x%x\n", inst->buffer_pool_id, ret);
        } else {
            printf("Successfully destroyed VB pool %d\n", inst->buffer_pool_id);
        }
        inst->buffer_pool_id = VB_INVALID_POOLID;
    }
}

/* Helper function to configure OSD attributes */
static int k230_display_configure_osd_attributes(lv_k230_display_intstance_t* inst, k_vo_video_osd_attr* osd_attr)
{
    if (!inst || !osd_attr) {
        return -1;
    }

    memset(osd_attr, 0, sizeof(*osd_attr));
    osd_attr->global_alptha   = 0xff; // Fully opaque
    osd_attr->display_rect.x  = 0;
    osd_attr->display_rect.y  = 0;
    osd_attr->img_size.width  = inst->panel_info.resolution.hdisplay;
    osd_attr->img_size.height = inst->panel_info.resolution.vdisplay;
    osd_attr->pixel_format    = inst->pixel_format;

    // Calculate stride based on pixel format
    switch (inst->pixel_format) {
    case PIXEL_FORMAT_RGB_565:
        osd_attr->stride = inst->panel_info.resolution.hdisplay * 2 / 8;
        break;
    case PIXEL_FORMAT_RGB_888:
        osd_attr->stride = inst->panel_info.resolution.hdisplay * 3 / 8;
        break;
    case PIXEL_FORMAT_ARGB_8888:
        osd_attr->stride = inst->panel_info.resolution.hdisplay * 4 / 8;
        break;
    default:
        printf("Unsupported pixel format for OSD\n");
        return -1;
    }

    return 0;
}

/* Unified OSD configuration function - automatically handles disable if needed */
static int k230_display_configure_osd(lv_k230_display_intstance_t* inst)
{
    k_vo_video_osd_attr osd_attr;
    k_s32               ret;

    if (!inst) {
        return -1;
    }

    // Always disable OSD first if it was already configured
    if (inst->layer_configured) {
        ret = kd_mpi_vo_osd_disable(inst->osd_layer);
        if (ret != K_SUCCESS) {
            printf("Warning: Failed to disable OSD: 0x%x\n", ret);
        }
        inst->layer_configured = 0;
    }

    // Configure OSD attributes
    if (k230_display_configure_osd_attributes(inst, &osd_attr) != 0) {
        return -1;
    }

    ret = kd_mpi_vo_set_video_osd_attr(inst->osd_layer, &osd_attr);
    if (ret != K_SUCCESS) {
        printf("Set OSD attr failed: 0x%x\n", ret);
        return -1;
    }

    ret = kd_mpi_vo_osd_enable(inst->osd_layer);
    if (ret != K_SUCCESS) {
        printf("Enable OSD failed: 0x%x\n", ret);
        return -1;
    }

    inst->layer_configured = 1;
    return 0;
}

static uint32_t tick_get_cb(void) { return (uint32_t)utils_cpu_ticks_ms(); }

static lv_k230_display_buffer_t* find_buffer_by_color_p(lv_k230_display_intstance_t* inst, uint8_t* color_p)
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
    static int show_rotate_buffer_flag = 0;

    lv_k230_display_intstance_t* inst = lv_display_get_driver_data(disp);
    k_s32                        ret;

    if (!inst || !inst->layer_configured) {
        lv_display_flush_ready(disp);
        return;
    }

    // Get current buffers
    lv_k230_display_buffer_t* show_buffer    = NULL;
    lv_k230_display_buffer_t* current_buffer = find_buffer_by_color_p(inst, color_p);

    if (!current_buffer) {
        printf("Invalid source buffer\n");
        lv_display_flush_ready(disp);
        return;
    }

    kd_mpi_sys_mmz_flush_cache(current_buffer->vf_info.v_frame.phys_addr[0], current_buffer->buffer_addr,
                               current_buffer->buffer_size);

    if (LV_DISPLAY_ROTATION_0 == inst->lv_rotation) {
        show_buffer = current_buffer;
    } else {
        // Use hardware GDMA rotation for non-0 degree rotations
        if (k230_display_rotate_using_gdma(inst, current_buffer, &inst->buffer_rotate) == 0) {
            show_buffer = &inst->buffer_rotate;
        } else {
            printf("GDMA rotation failed\n");
        }
    }

    // Validate show_buffer
    if (!show_buffer || !show_buffer->buffer_addr) {
        printf("Invalid show buffer\n");
        lv_display_flush_ready(disp);
        return;
    }

    if ((0x00 == show_rotate_buffer_flag) || (show_buffer != &inst->buffer_rotate)) {
        // Insert frame to OSD layer
        ret = kd_mpi_vo_chn_insert_frame(inst->osd_layer + 3, &show_buffer->vf_info);
        if (ret != K_SUCCESS) {
            printf("Insert frame failed: 0x%x\n", ret);
        }

        if (show_buffer == &inst->buffer_rotate) {
            show_rotate_buffer_flag = 1;
        } else {
            show_rotate_buffer_flag = 0;
        }
    }

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
            if (inst->layer_configured) {
                kd_mpi_vo_osd_disable(inst->osd_layer);
            }

            // Clean up buffers
            k230_display_buffer_deinit(inst);

            // Clean up DMA
            k230_display_dma_deinit(inst);

            lv_display_set_driver_data(display, NULL);
            lv_free(inst);
        }
        break;
    case LV_EVENT_RESOLUTION_CHANGED:
        if (inst) {
            int32_t new_width  = lv_display_get_horizontal_resolution(display);
            int32_t new_height = lv_display_get_vertical_resolution(display);

            printf("Resolution changed event: %dx%d\n", new_width, new_height);

            inst->lv_rotation = lv_display_get_rotation(display);
        }
        break;
    case LV_EVENT_COLOR_FORMAT_CHANGED:
        if (inst) {
            lv_color_format_t new_color_format = lv_display_get_color_format(display);
            printf("Color format changed event: %d -> %d\n", inst->color_format, new_color_format);

            // Check if the new format is supported
            k_pixel_format new_pixel_format = lv_k230_map_color_format_to_pixel_format(new_color_format);
            if (PIXEL_FORMAT_BUTT == new_pixel_format) {
                printf("Unsupported color format: %d\n", new_color_format);
                // Revert to old format
                lv_display_set_color_format(display, inst->color_format);
                break;
            }

            // If format is the same, no need to reconfigure
            if (new_color_format == inst->color_format) {
                printf("Color format unchanged, skipping reconfiguration\n");
                break;
            }

            printf("Reconfiguring display for new color format...\n");

            // Disable OSD layer temporarily during reconfiguration
            if (inst->layer_configured) {
                kd_mpi_vo_osd_disable(inst->osd_layer);
                inst->layer_configured = 0;
            }

            // Store old format for rollback
            lv_color_format_t old_color_format = inst->color_format;
            k_pixel_format    old_pixel_format = inst->pixel_format;

            // Update instance with new format temporarily
            inst->color_format = new_color_format;
            inst->pixel_format = new_pixel_format;

            // Reconfigure buffers for new color format
            if (k230_display_configure_buffers(inst) != 0) {
                printf("Failed to reconfigure buffers for new color format\n");
                // Rollback to old format
                inst->color_format = old_color_format;
                inst->pixel_format = old_pixel_format;
                lv_display_set_color_format(display, old_color_format);
                // Try to restore OSD with old format
                k230_display_configure_osd(inst);
                break;
            }

            // Reconfigure OSD for new color format
            if (k230_display_configure_osd(inst) != 0) {
                printf("Failed to reconfigure OSD for new color format\n");
                // Rollback: reconfigure buffers back to old format
                inst->color_format = old_color_format;
                inst->pixel_format = old_pixel_format;
                k230_display_configure_buffers(inst);
                lv_display_set_color_format(display, old_color_format);
                // Try to restore OSD with old format
                k230_display_configure_osd(inst);
                break;
            }

            // Successfully updated color format
            inst->layer_configured = 1;
            printf("Successfully changed color format from %d to %d\n", old_color_format, new_color_format);
        }
        break;
    default:
        return;
    }
}

static k_pixel_format lv_k230_map_color_format_to_pixel_format(lv_color_format_t color_format)
{
    switch (color_format) {
    case LV_COLOR_FORMAT_RGB565:
        return PIXEL_FORMAT_RGB_565;
    case LV_COLOR_FORMAT_RGB888:
        return PIXEL_FORMAT_RGB_888;
    case LV_COLOR_FORMAT_ARGB8888:
    case LV_COLOR_FORMAT_XRGB8888:
        return PIXEL_FORMAT_ARGB_8888;
    default:
        return PIXEL_FORMAT_BUTT;
    }
}

/* DMA initialization and management functions */
static int k230_display_dma_init(lv_k230_display_intstance_t* inst)
{
    k_s32 ret;

    if (!inst) {
        printf("DMA init: invalid instance\n");
        return -1;
    }

    /* Request GDMA channel */
    inst->dma_chn = kd_mpi_dma_request_chn(GDMA_TYPE);
    if (inst->dma_chn < 0) {
        printf("DMA init: failed to request GDMA channel\n");
        return -1;
    }

    inst->dma_initialized = true;
    printf("DMA initialized successfully on channel %d\n", inst->dma_chn);
    return 0;
}

static void k230_display_dma_deinit(lv_k230_display_intstance_t* inst)
{
    if (!inst || !inst->dma_initialized) {
        return;
    }

    /* Stop DMA channel if active */
    if (inst->dma_chn >= 0) {
        kd_mpi_dma_stop_chn(inst->dma_chn);
        kd_mpi_dma_release_chn(inst->dma_chn);
        inst->dma_chn = -1;
    }

    inst->dma_initialized = false;
    printf("DMA deinitialized\n");
}

/* Helper functions for DMA rotation */
static k_gdma_rotation_e get_dma_rotation(int flag)
{
    if (flag & K_ROTATION_90)
        return DEGREE_90;
    if (flag & K_ROTATION_180)
        return DEGREE_180;
    if (flag & K_ROTATION_270)
        return DEGREE_270;
    return DEGREE_0;
}

static k_pixel_format_dma_e get_dma_pixel_format(k_pixel_format pixel_format)
{
    switch (pixel_format) {
    case PIXEL_FORMAT_ARGB_8888:
    case PIXEL_FORMAT_ABGR_8888:
    case PIXEL_FORMAT_BGRA_8888:
        return DMA_PIXEL_FORMAT_ARGB_8888;

    case PIXEL_FORMAT_RGB_888:
    case PIXEL_FORMAT_BGR_888:
        return DMA_PIXEL_FORMAT_RGB_888;

    case PIXEL_FORMAT_RGB_565:
    case PIXEL_FORMAT_RGB_565_LE:
    case PIXEL_FORMAT_BGR_565_LE:
        return DMA_PIXEL_FORMAT_RGB_565;

    case PIXEL_FORMAT_RGB_MONOCHROME_8BPP:
        return DMA_PIXEL_FORMAT_YUV_400_8BIT;

    default:
        return DMA_PIXEL_FORMAT_BUTT;
    }
}

static k_dma_chn_attr_u generate_dma_attributes(int flag, k_video_frame_info* in, k_video_frame_info* out)
{
    k_dma_chn_attr_u attr = {
        .gdma_attr.buffer_num    = 1,
        .gdma_attr.rotation      = get_dma_rotation(flag),
        .gdma_attr.x_mirror      = (flag & (K_VO_MIRROR_HOR | K_VO_MIRROR_BOTH)) ? K_TRUE : K_FALSE,
        .gdma_attr.y_mirror      = (flag & (K_VO_MIRROR_VER | K_VO_MIRROR_BOTH)) ? K_TRUE : K_FALSE,
        .gdma_attr.width         = in->v_frame.width,
        .gdma_attr.height        = in->v_frame.height,
        .gdma_attr.src_stride[0] = in->v_frame.stride[0],
        .gdma_attr.src_stride[1] = 0,
        .gdma_attr.src_stride[2] = 0,
        .gdma_attr.dst_stride[0] = out->v_frame.stride[0],
        .gdma_attr.dst_stride[1] = 0,
        .gdma_attr.dst_stride[2] = 0,
        .gdma_attr.work_mode     = DMA_UNBIND,
        .gdma_attr.pixel_format  = get_dma_pixel_format(in->v_frame.pixel_format),
    };

    return attr;
}

#if 0 
void dump_gdma_chn_attr(const k_gdma_chn_attr_t* attr, const char* name)
{
    if (!attr) {
        printf("Error: NULL pointer passed to dump_gdma_chn_attr\n");
        return;
    }

    printf("=== GDMA Channel Attributes: %s ===\n", name ? name : "unnamed");
    printf("  buffer_num:    %u\n", attr->buffer_num);

    // Convert rotation enum to string
    const char* rotation_str;
    switch (attr->lv_rotation) {
    case DEGREE_0:
        rotation_str = "DEGREE_0";
        break;
    case DEGREE_90:
        rotation_str = "DEGREE_90";
        break;
    case DEGREE_180:
        rotation_str = "DEGREE_180";
        break;
    case DEGREE_270:
        rotation_str = "DEGREE_270";
        break;
    default:
        rotation_str = "UNKNOWN";
        break;
    }
    printf("  rotation:      %s (%d)\n", rotation_str, attr->lv_rotation);

    printf("  x_mirror:      %s\n", attr->x_mirror ? "TRUE" : "FALSE");
    printf("  y_mirror:      %s\n", attr->y_mirror ? "TRUE" : "FALSE");
    printf("  width:         %u pixels\n", attr->width);
    printf("  height:        %u pixels\n", attr->height);

    printf("  src_stride:    [%u, %u, %u]\n", attr->src_stride[0], attr->src_stride[1], attr->src_stride[2]);
    printf("  dst_stride:    [%u, %u, %u]\n", attr->dst_stride[0], attr->dst_stride[1], attr->dst_stride[2]);

    // Convert work mode enum to string
    const char* work_mode_str;
    switch (attr->work_mode) {
    case DMA_BIND:
        work_mode_str = "DMA_BIND";
        break;
    case DMA_UNBIND:
        work_mode_str = "DMA_UNBIND";
        break;
    default:
        work_mode_str = "UNKNOWN";
        break;
    }
    printf("  work_mode:     %s (%d)\n", work_mode_str, attr->work_mode);

    // Convert pixel format enum to string (partial list - extend as needed)
    const char* pixel_format_str;
    switch (attr->pixel_format) {
    case DMA_PIXEL_FORMAT_ARGB_8888:
        pixel_format_str = "DMA_PIXEL_FORMAT_ARGB_8888";
        break;
    case DMA_PIXEL_FORMAT_RGB_888:
        pixel_format_str = "DMA_PIXEL_FORMAT_RGB_888";
        break;
    case DMA_PIXEL_FORMAT_RGB_565:
        pixel_format_str = "DMA_PIXEL_FORMAT_RGB_565";
        break;
    case DMA_PIXEL_FORMAT_YUV_400_8BIT:
        pixel_format_str = "DMA_PIXEL_FORMAT_YUV_400_8BIT";
        break;
    // case DMA_PIXEL_FORMAT_BUTT:      pixel_format_str = "DMA_PIXEL_FORMAT_BUTT"; break;
    default:
        pixel_format_str = "UNKNOWN";
        break;
    }
    printf("  pixel_format:  %s (%d)\n", pixel_format_str, attr->pixel_format);
    printf("==========================================\n");
}

/* Function to save frame data to PGM/PPM file */
static int save_frame_to_ppm(const k_video_frame_info* vf_info, const char* filename)
{
    FILE* file        = NULL;
    void* mapped_addr = NULL;
    int   ret         = 0;

    if (!vf_info || !filename) {
        printf("Error: Invalid parameters for save_frame_to_ppm\n");
        return -1;
    }

    file = fopen(filename, "wb");
    if (!file) {
        printf("Error: Failed to open file %s for writing\n", filename);
        return -1;
    }

    // Map physical address to virtual address
    if (vf_info->v_frame.phys_addr[0] != 0) {
        // Calculate frame size based on stride and height
        uint32_t frame_size = vf_info->v_frame.stride[0] * vf_info->v_frame.height;

        mapped_addr = kd_mpi_sys_mmap_cached(vf_info->v_frame.phys_addr[0], frame_size);
        if (!mapped_addr) {
            printf("Error: Failed to mmap physical address 0x%lx for frame data\n", vf_info->v_frame.phys_addr[0]);
            ret = -1;
            goto cleanup;
        }

        // Flush cache to ensure we have the latest data
        kd_mpi_sys_mmz_flush_cache(vf_info->v_frame.phys_addr[0], mapped_addr, frame_size);

        // Write PPM/PGM header based on pixel format
        const char* format_str = "";
        int         max_value  = 255;
        int         components = 1;

        switch (vf_info->v_frame.pixel_format) {
        case PIXEL_FORMAT_RGB_565:
            format_str = "P6"; // RGB PPM
            components = 3;
            fprintf(file, "P6\n%d %d\n%d\n", vf_info->v_frame.width, vf_info->v_frame.height, max_value);
            break;
        case PIXEL_FORMAT_RGB_888:
            format_str = "P6"; // RGB PPM
            components = 3;
            fprintf(file, "P6\n%d %d\n%d\n", vf_info->v_frame.width, vf_info->v_frame.height, max_value);
            break;
        case PIXEL_FORMAT_ARGB_8888:
            format_str = "P6"; // RGB PPM (ignore alpha)
            components = 3;
            fprintf(file, "P6\n%d %d\n%d\n", vf_info->v_frame.width, vf_info->v_frame.height, max_value);
            break;
        case PIXEL_FORMAT_RGB_MONOCHROME_8BPP:
            format_str = "P5"; // Grayscale PGM
            components = 1;
            fprintf(file, "P5\n%d %d\n%d\n", vf_info->v_frame.width, vf_info->v_frame.height, max_value);
            break;
        default:
            printf("Unsupported pixel format for PPM/PGM: %d\n", vf_info->v_frame.pixel_format);
            ret = -1;
            goto cleanup;
        }

        printf("Saving %dx%d frame as %s (format: %d -> %s, %d components)\n", vf_info->v_frame.width, vf_info->v_frame.height,
               filename, vf_info->v_frame.pixel_format, format_str, components);

        // Convert and write pixel data
        uint8_t* src_data   = (uint8_t*)mapped_addr;
        uint32_t src_stride = vf_info->v_frame.stride[0];
        uint32_t dst_stride = vf_info->v_frame.width * components;

        for (int y = 0; y < vf_info->v_frame.height; y++) {
            uint8_t* src_line = src_data + (y * src_stride);
            uint8_t* dst_line = malloc(dst_stride);

            if (!dst_line) {
                printf("Error: Failed to allocate memory for line %d\n", y);
                ret = -1;
                goto cleanup;
            }

            // Convert pixel format to RGB24 for PPM
            switch (vf_info->v_frame.pixel_format) {
            case PIXEL_FORMAT_RGB_565: {
                // Convert RGB565 to RGB888
                uint16_t* src_pixel = (uint16_t*)src_line;
                uint8_t*  dst_pixel = dst_line;
                for (int x = 0; x < vf_info->v_frame.width; x++) {
                    uint16_t pixel = src_pixel[x];
                    dst_pixel[0]   = ((pixel >> 11) & 0x1F) << 3; // R
                    dst_pixel[1]   = ((pixel >> 5) & 0x3F) << 2; // G
                    dst_pixel[2]   = (pixel & 0x1F) << 3; // B
                    dst_pixel += 3;
                }
                break;
            }
            case PIXEL_FORMAT_RGB_888: {
                // Direct copy for RGB888
                memcpy(dst_line, src_line, dst_stride);
                break;
            }
            case PIXEL_FORMAT_ARGB_8888: {
                // Convert ARGB8888 to RGB888 (skip alpha)
                uint32_t* src_pixel = (uint32_t*)src_line;
                uint8_t*  dst_pixel = dst_line;
                for (int x = 0; x < vf_info->v_frame.width; x++) {
                    uint32_t pixel = src_pixel[x];
                    dst_pixel[0]   = (pixel >> 16) & 0xFF; // R
                    dst_pixel[1]   = (pixel >> 8) & 0xFF; // G
                    dst_pixel[2]   = pixel & 0xFF; // B
                    dst_pixel += 3;
                }
                break;
            }
            case PIXEL_FORMAT_RGB_MONOCHROME_8BPP: {
                // Direct copy for grayscale
                memcpy(dst_line, src_line, dst_stride);
                break;
            }
            }

            // Write converted line
            size_t written = fwrite(dst_line, 1, dst_stride, file);
            if (written != dst_stride) {
                printf("Error: Failed to write line %d to file %s\n", y, filename);
                free(dst_line);
                ret = -1;
                goto cleanup;
            }

            free(dst_line);
        }

        printf("Successfully saved frame to %s (%dx%d, format: %s)\n", filename, vf_info->v_frame.width,
               vf_info->v_frame.height, format_str);

    } else {
        printf("Warning: No physical address available\n");
        ret = -1;
    }

cleanup:
    // Unmap the physical address
    if (mapped_addr && vf_info->v_frame.phys_addr[0] != 0) {
        kd_mpi_sys_munmap(mapped_addr, vf_info->v_frame.stride[0] * vf_info->v_frame.height);
    }

    if (file) {
        fclose(file);
    }
    return ret;
}
#endif

static int k230_display_rotate_using_gdma(lv_k230_display_intstance_t* inst, lv_k230_display_buffer_t* src_buf,
                                          lv_k230_display_buffer_t* dst_buf)
{
    k_s32              ret;
    k_dma_chn_attr_u   chn_attr;
    k_video_frame_info tmp_frame;
    k_video_frame_info src_frame_info; // Temporary source frame with LVGL dimensions
    int                panel_width   = inst->panel_info.resolution.hdisplay;
    int                panel_height  = inst->panel_info.resolution.vdisplay;
    int                rotation_flag = 0;
    int                lvgl_width, lvgl_height;

    static k_dma_chn_attr_u last_chn_attr = { 0 };

    if (!inst || !inst->dma_initialized || inst->dma_chn < 0) {
        printf("GDMA rotation: DMA not initialized\n");
        return -1;
    }

    /* Get bits per pixel from color format and calculate bytes per pixel */
    uint32_t bytes_per_pixel;
    switch (inst->color_format) {
    case LV_COLOR_FORMAT_RGB565:
        bytes_per_pixel = 2;
        break;
    case LV_COLOR_FORMAT_RGB888:
        bytes_per_pixel = 3;
        break;
    case LV_COLOR_FORMAT_ARGB8888:
    case LV_COLOR_FORMAT_XRGB8888:
        bytes_per_pixel = 4;
        break;
    default:
        printf("GDMA rotation: unsupported color format\n");
        return -1;
    }

    /* Get LVGL's expected dimensions after rotation */
    lvgl_width  = lv_display_get_horizontal_resolution((lv_display_t*)inst->lv_disp);
    lvgl_height = lv_display_get_vertical_resolution((lv_display_t*)inst->lv_disp);

    // printf("LVGL resolution: %dx%d, Panel resolution: %dx%d\n", lvgl_width, lvgl_height, panel_width, panel_height);

    switch (inst->lv_rotation) {
    case LV_DISPLAY_ROTATION_90:
        /* LVGL 90° counter-clockwise = DMA 270° clockwise */
        rotation_flag = K_ROTATION_270;
        break;
    case LV_DISPLAY_ROTATION_180:
        rotation_flag = K_ROTATION_180;
        break;
    case LV_DISPLAY_ROTATION_270:
        /* LVGL 270° counter-clockwise = DMA 90° clockwise */
        rotation_flag = K_ROTATION_90;
        break;
    case LV_DISPLAY_ROTATION_0:
    default:
        /* No rotation needed, just copy */
        memcpy(dst_buf->buffer_addr, src_buf->buffer_addr, src_buf->buffer_size);
        return 0;
    }

    /* Create temporary source frame info with LVGL's dimensions */
    memset(&src_frame_info, 0, sizeof(src_frame_info));
    src_frame_info.mod_id               = K_ID_VO;
    src_frame_info.pool_id              = src_buf->vf_info.pool_id;
    src_frame_info.v_frame.width        = lvgl_width;
    src_frame_info.v_frame.height       = lvgl_height;
    src_frame_info.v_frame.stride[0]    = lvgl_width * bytes_per_pixel;
    src_frame_info.v_frame.pixel_format = inst->pixel_format;
    src_frame_info.v_frame.phys_addr[0] = src_buf->vf_info.v_frame.phys_addr[0];
    src_frame_info.v_frame.virt_addr[0] = (uint64_t)src_buf->buffer_addr;

    // printf("GDMA rotation: LVGL %dx%d -> Panel %dx%d, rotation_flag: 0x%x\n", lvgl_width, lvgl_height, panel_width,
    //        panel_height, rotation_flag);
    // printf("Source stride: %d, Dest stride: %d\n", src_frame_info.v_frame.stride[0], dst_buf->vf_info.v_frame.stride[0]);

    /* Generate DMA attributes using the temporary source frame */
    chn_attr = generate_dma_attributes(rotation_flag, &src_frame_info, &dst_buf->vf_info);

    /* Check if pixel format is supported */
    if (chn_attr.gdma_attr.pixel_format == DMA_PIXEL_FORMAT_BUTT) {
        printf("GDMA rotation: unsupported pixel format for DMA\n");
        return -1;
    }

    if (0x00 != memcmp(&last_chn_attr, &chn_attr, sizeof(last_chn_attr))) {
        /* Stop and configure DMA channel */
        kd_mpi_dma_stop_chn(inst->dma_chn);
        ret = kd_mpi_dma_set_chn_attr(inst->dma_chn, &chn_attr);
        if (ret != K_SUCCESS) {
            printf("GDMA rotation: set channel attr failed: 0x%x\n", ret);
            return -1;
        }

        /* Start DMA channel */
        ret = kd_mpi_dma_start_chn(inst->dma_chn);
        if (ret != K_SUCCESS) {
            printf("GDMA rotation: start channel failed: 0x%x\n", ret);
            return -1;
        }

        memcpy(&last_chn_attr, &chn_attr, sizeof(last_chn_attr));
    }

    /* Send frame for rotation - use the temporary source frame */
    ret = kd_mpi_dma_send_frame(inst->dma_chn, &src_frame_info, -1);
    if (ret != K_SUCCESS) {
        printf("GDMA rotation: send frame failed: 0x%x\n", ret);
        return -1;
    }

    /* Get rotated frame */
    ret = kd_mpi_dma_get_frame(inst->dma_chn, &tmp_frame, -1);
    if (ret != K_SUCCESS) {
        printf("GDMA rotation: get frame failed: 0x%x\n", ret);
        return -1;
    }

    /* Copy rotated data to display buffer */
    uint32_t size = dst_buf->vf_info.v_frame.stride[0] * dst_buf->vf_info.v_frame.height;

    void* tmp_addr = kd_mpi_sys_mmap_cached(tmp_frame.v_frame.phys_addr[0], size);
    if (tmp_addr) {
        kd_mpi_sys_mmz_flush_cache(tmp_frame.v_frame.phys_addr[0], tmp_addr, size);
        memcpy(dst_buf->buffer_addr, tmp_addr, size);
        kd_mpi_sys_munmap(tmp_addr, size);
    } else {
        printf("GDMA rotation: failed to mmap temporary buffer\n");
        kd_mpi_dma_release_frame(inst->dma_chn, &tmp_frame);
        return -1;
    }

    /* Release DMA frame */
    kd_mpi_dma_release_frame(inst->dma_chn, &tmp_frame);

    // printf("GDMA rotation completed successfully\n");

    return 0;
}
