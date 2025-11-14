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

    lv_color_format_t color_format;
    k_pixel_format    pixel_format;

    int      layer_configured;
    uint32_t layer_flag;
    int      buffer_count; /* buffer for lvgl count */

    k_u32                    buffer_pool_id; /* buffer pool id */
    lv_k230_display_buffer_t buffer[2]; /* for lvgl use */
    lv_k230_display_buffer_t buffer_display; /* for hardware use,  */

    lv_display_t* lvgl_disp;

    int           current_buffer; /* current buffer index for double buffering */
    k_vo_pub_attr vo_attr; /* VO attributes */

    /* Rotation and resolution support */
    lv_display_rotation_t rotation;

    /* DMA support for hardware rotation */
    k_s32 dma_chn; /* DMA channel for rotation */
    bool  dma_initialized; /* DMA initialization flag */
} lv_k230_display_intstance_t;

static int            k230_display_buffer_init(lv_k230_display_intstance_t* inst);
static int            k230_display_osd_init(lv_k230_display_intstance_t* inst);
static void           k230_display_buffer_deinit(lv_k230_display_intstance_t* inst);
static int            k230_display_reconfigure_buffers(lv_k230_display_intstance_t* inst, lv_color_format_t new_color_format);
static int            k230_display_reconfigure_osd(lv_k230_display_intstance_t* inst);
static k_pixel_format lv_k230_map_color_format_to_pixel_format(lv_color_format_t color_format);

static uint32_t tick_get_cb(void);
static void     flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* color_p);
static void     event_cb(lv_event_t* e);

/* Rotation and resolution support functions */
static void k230_display_apply_rotation(lv_k230_display_intstance_t* inst);

/* DMA support functions */
static int  k230_display_dma_init(lv_k230_display_intstance_t* inst);
static void k230_display_dma_deinit(lv_k230_display_intstance_t* inst);
static int  k230_display_rotate_using_gdma(lv_k230_display_intstance_t* inst);

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
    inst->layer_flag     = 0;
    inst->buffer_count   = 2;
    inst->current_buffer = 0;
    inst->buffer_pool_id = VB_INVALID_POOLID; /* Will be created internally */
    lv_memcpy(&inst->panel_info, &info, sizeof(inst->panel_info));

    /* Initialize rotation state */
    inst->rotation  = LV_DISPLAY_ROTATION_0;
    inst->lvgl_disp = disp;

    /* Initialize DMA state */
    inst->dma_chn         = -1;
    inst->dma_initialized = false;

    inst->color_format = lv_display_get_color_format(disp);
    inst->pixel_format = lv_k230_map_color_format_to_pixel_format(inst->color_format);
    if (PIXEL_FORMAT_BUTT == inst->pixel_format) {
        printf("Unsupported color format\n");
        goto _failed_map_color_format;
    }

    if (0x00 != k230_display_buffer_init(inst)) {
        printf("Buffer init failed\n");
        goto _failed_buffer_init;
    }

    if (0x00 != k230_display_osd_init(inst)) {
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

static int k230_display_buffer_init(lv_k230_display_intstance_t* inst)
{
    k_s32  ret;
    int    i;
    size_t buffer_size;

    if (!inst) {
        return -1;
    }

    // Calculate buffer size based on color format
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
        return -1;
    }

    // Add some extra space for alignment
    buffer_size = (buffer_size + 4095) & ~4095; // Align to 4K boundary

    // Create VB pool internally for display buffers
    k_vb_pool_config pool_config;
    memset(&pool_config, 0, sizeof(pool_config));

    // We need 3 buffers: 2 for LVGL double buffering + 1 for display
    pool_config.blk_cnt  = inst->buffer_count + 1;
    pool_config.blk_size = buffer_size;
    pool_config.mode     = VB_REMAP_MODE_CACHED; // Use cached mode for better performance

    inst->buffer_pool_id = kd_mpi_vb_create_pool(&pool_config);
    if (inst->buffer_pool_id == VB_INVALID_POOLID) {
        printf("Failed to create VB pool for display\n");
        return -1;
    }

    printf("Created VB pool %d with %d blocks of size %zu\n", inst->buffer_pool_id, pool_config.blk_cnt, buffer_size);

    // Allocate buffers for LVGL
    for (i = 0; i < inst->buffer_count; i++) {
        inst->buffer[i].block_handle = kd_mpi_vb_get_block(inst->buffer_pool_id, buffer_size, NULL);
        if (inst->buffer[i].block_handle == VB_INVALID_HANDLE) {
            printf("Get VB block failed for buffer %d\n", i);
            // Clean up previously allocated buffers
            for (int j = 0; j < i; j++) {
                kd_mpi_vb_release_block(inst->buffer[j].block_handle);
            }
            // Destroy the pool we created since allocation failed
            kd_mpi_vb_destory_pool(inst->buffer_pool_id);
            return -1;
        }

        inst->buffer[i].buffer_size                  = buffer_size;
        inst->buffer[i].vf_info.v_frame.phys_addr[0] = kd_mpi_vb_handle_to_phyaddr(inst->buffer[i].block_handle);
        inst->buffer[i].buffer_addr = kd_mpi_sys_mmap(inst->buffer[i].vf_info.v_frame.phys_addr[0], buffer_size);

        if (!inst->buffer[i].buffer_addr) {
            printf("Mmap failed for buffer %d\n", i);
            kd_mpi_vb_release_block(inst->buffer[i].block_handle);
            // Clean up previously allocated buffers
            for (int j = 0; j < i; j++) {
                kd_mpi_sys_munmap(inst->buffer[j].buffer_addr, buffer_size);
                kd_mpi_vb_release_block(inst->buffer[j].block_handle);
            }
            // Destroy the pool we created since mmap failed
            kd_mpi_vb_destory_pool(inst->buffer_pool_id);
            return -1;
        }

        // Setup frame info
        inst->buffer[i].vf_info.mod_id               = K_ID_VO;
        inst->buffer[i].vf_info.pool_id              = inst->buffer_pool_id;
        inst->buffer[i].vf_info.v_frame.width        = inst->panel_info.resolution.hdisplay;
        inst->buffer[i].vf_info.v_frame.height       = inst->panel_info.resolution.vdisplay;
        inst->buffer[i].vf_info.v_frame.stride[0]    = inst->panel_info.resolution.hdisplay;
        inst->buffer[i].vf_info.v_frame.pixel_format = inst->pixel_format;
        inst->buffer[i].vf_info.v_frame.priv_data    = K_VO_ONLY_CHANGE_PHYADDR;
    }

    // Allocate display buffer
    inst->buffer_display.block_handle = kd_mpi_vb_get_block(inst->buffer_pool_id, buffer_size, NULL);
    if (inst->buffer_display.block_handle == VB_INVALID_HANDLE) {
        printf("Get display VB block failed\n");
        k230_display_buffer_deinit(inst);
        return -1;
    }

    inst->buffer_display.buffer_size                  = buffer_size;
    inst->buffer_display.vf_info.v_frame.phys_addr[0] = kd_mpi_vb_handle_to_phyaddr(inst->buffer_display.block_handle);
    inst->buffer_display.buffer_addr = kd_mpi_sys_mmap(inst->buffer_display.vf_info.v_frame.phys_addr[0], buffer_size);

    if (!inst->buffer_display.buffer_addr) {
        printf("Display mmap failed\n");
        kd_mpi_vb_release_block(inst->buffer_display.block_handle);
        k230_display_buffer_deinit(inst);
        return -1;
    }

    // Setup display frame info
    inst->buffer_display.vf_info.mod_id               = K_ID_VO;
    inst->buffer_display.vf_info.pool_id              = inst->buffer_pool_id;
    inst->buffer_display.vf_info.v_frame.width        = inst->panel_info.resolution.hdisplay;
    inst->buffer_display.vf_info.v_frame.height       = inst->panel_info.resolution.vdisplay;
    inst->buffer_display.vf_info.v_frame.stride[0]    = inst->panel_info.resolution.hdisplay;
    inst->buffer_display.vf_info.v_frame.pixel_format = inst->pixel_format;
    inst->buffer_display.vf_info.v_frame.priv_data    = K_VO_ONLY_CHANGE_PHYADDR;

    return 0;
}

static int k230_display_reconfigure_buffers(lv_k230_display_intstance_t* inst, lv_color_format_t new_color_format)
{
    k_pixel_format new_pixel_format = lv_k230_map_color_format_to_pixel_format(new_color_format);
    size_t         new_buffer_size;
    k_s32          ret;
    int            i;

    if (!inst) {
        return -1;
    }

    // Calculate new buffer size based on new color format
    switch (new_pixel_format) {
    case PIXEL_FORMAT_RGB_565:
        new_buffer_size = inst->panel_info.resolution.hdisplay * inst->panel_info.resolution.vdisplay * 2;
        break;
    case PIXEL_FORMAT_RGB_888:
        new_buffer_size = inst->panel_info.resolution.hdisplay * inst->panel_info.resolution.vdisplay * 3;
        break;
    case PIXEL_FORMAT_ARGB_8888:
        new_buffer_size = inst->panel_info.resolution.hdisplay * inst->panel_info.resolution.vdisplay * 4;
        break;
    default:
        printf("Unsupported pixel format for buffer reconfiguration\n");
        return -1;
    }

    // Add some extra space for alignment
    new_buffer_size = (new_buffer_size + 4095) & ~4095; // Align to 4K boundary

    printf("Reconfiguring buffers: format %d->%d, size %zu->%zu bytes\n", inst->pixel_format, new_pixel_format,
           inst->buffer[0].buffer_size, new_buffer_size);

    // Calculate stride in bytes and convert to 8-byte units
    uint32_t bytes_per_pixel;
    switch (new_pixel_format) {
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

    uint32_t stride_bytes = inst->panel_info.resolution.hdisplay * bytes_per_pixel;
    uint32_t stride_8byte = (stride_bytes + 7) / 8;

    // Destroy old VB pool if it exists
    if (inst->buffer_pool_id != VB_INVALID_POOLID) {
        k230_display_buffer_deinit(inst);
    }

    // Create new VB pool with new buffer size
    k_vb_pool_config pool_config;
    memset(&pool_config, 0, sizeof(pool_config));
    pool_config.blk_cnt  = inst->buffer_count + 1;
    pool_config.blk_size = new_buffer_size;
    pool_config.mode     = VB_REMAP_MODE_CACHED;

    inst->buffer_pool_id = kd_mpi_vb_create_pool(&pool_config);
    if (inst->buffer_pool_id == VB_INVALID_POOLID) {
        printf("Failed to create new VB pool for color format change\n");
        return -1;
    }

    printf("Created new VB pool %d with %d blocks of size %zu\n", inst->buffer_pool_id, pool_config.blk_cnt, new_buffer_size);

    // Allocate new buffers for LVGL
    for (i = 0; i < inst->buffer_count; i++) {
        inst->buffer[i].block_handle = kd_mpi_vb_get_block(inst->buffer_pool_id, new_buffer_size, NULL);
        if (inst->buffer[i].block_handle == VB_INVALID_HANDLE) {
            printf("Get new VB block failed for buffer %d\n", i);
            // Clean up previously allocated buffers
            for (int j = 0; j < i; j++) {
                kd_mpi_vb_release_block(inst->buffer[j].block_handle);
            }
            kd_mpi_vb_destory_pool(inst->buffer_pool_id);
            inst->buffer_pool_id = VB_INVALID_POOLID;
            return -1;
        }

        inst->buffer[i].buffer_size                  = new_buffer_size;
        inst->buffer[i].vf_info.v_frame.phys_addr[0] = kd_mpi_vb_handle_to_phyaddr(inst->buffer[i].block_handle);
        inst->buffer[i].buffer_addr = kd_mpi_sys_mmap(inst->buffer[i].vf_info.v_frame.phys_addr[0], new_buffer_size);

        if (!inst->buffer[i].buffer_addr) {
            printf("Mmap failed for new buffer %d\n", i);
            kd_mpi_vb_release_block(inst->buffer[i].block_handle);
            // Clean up previously allocated buffers
            for (int j = 0; j < i; j++) {
                kd_mpi_sys_munmap(inst->buffer[j].buffer_addr, new_buffer_size);
                kd_mpi_vb_release_block(inst->buffer[j].block_handle);
            }
            kd_mpi_vb_destory_pool(inst->buffer_pool_id);
            inst->buffer_pool_id = VB_INVALID_POOLID;
            return -1;
        }

        // Update frame info with new pixel format and stride
        inst->buffer[i].vf_info.mod_id               = K_ID_VO;
        inst->buffer[i].vf_info.pool_id              = inst->buffer_pool_id;
        inst->buffer[i].vf_info.v_frame.width        = inst->panel_info.resolution.hdisplay;
        inst->buffer[i].vf_info.v_frame.height       = inst->panel_info.resolution.vdisplay;
        inst->buffer[i].vf_info.v_frame.stride[0]    = stride_8byte;
        inst->buffer[i].vf_info.v_frame.pixel_format = new_pixel_format;
        inst->buffer[i].vf_info.v_frame.priv_data    = K_VO_ONLY_CHANGE_PHYADDR;
    }

    // Allocate new display buffer
    inst->buffer_display.block_handle = kd_mpi_vb_get_block(inst->buffer_pool_id, new_buffer_size, NULL);
    if (inst->buffer_display.block_handle == VB_INVALID_HANDLE) {
        printf("Get new display VB block failed\n");
        k230_display_buffer_deinit(inst);
        return -1;
    }

    inst->buffer_display.buffer_size                  = new_buffer_size;
    inst->buffer_display.vf_info.v_frame.phys_addr[0] = kd_mpi_vb_handle_to_phyaddr(inst->buffer_display.block_handle);
    inst->buffer_display.buffer_addr = kd_mpi_sys_mmap(inst->buffer_display.vf_info.v_frame.phys_addr[0], new_buffer_size);

    if (!inst->buffer_display.buffer_addr) {
        printf("Display mmap failed for new format\n");
        kd_mpi_vb_release_block(inst->buffer_display.block_handle);
        k230_display_buffer_deinit(inst);
        return -1;
    }

    // Update display frame info
    inst->buffer_display.vf_info.mod_id               = K_ID_VO;
    inst->buffer_display.vf_info.pool_id              = inst->buffer_pool_id;
    inst->buffer_display.vf_info.v_frame.width        = inst->panel_info.resolution.hdisplay;
    inst->buffer_display.vf_info.v_frame.height       = inst->panel_info.resolution.vdisplay;
    inst->buffer_display.vf_info.v_frame.stride[0]    = stride_8byte;
    inst->buffer_display.vf_info.v_frame.pixel_format = new_pixel_format;
    inst->buffer_display.vf_info.v_frame.priv_data    = K_VO_ONLY_CHANGE_PHYADDR;

    // Reset current buffer index
    inst->current_buffer = 0;

    // Update LVGL display buffers
    lv_display_set_buffers(inst->lvgl_disp, inst->buffer[0].buffer_addr, inst->buffer[1].buffer_addr,
                           inst->buffer[0].buffer_size, LV_DISPLAY_RENDER_MODE_DIRECT);

    printf("Successfully reconfigured buffers for new color format\n");
    return 0;
}

static int k230_display_reconfigure_osd(lv_k230_display_intstance_t* inst)
{
    k_vo_video_osd_attr osd_attr;
    k_s32               ret;

    if (!inst) {
        return -1;
    }

    printf("Reconfiguring OSD for pixel format: %d\n", inst->pixel_format);

    // Disable OSD layer first if it was enabled
    if (inst->layer_configured) {
        ret = kd_mpi_vo_osd_disable(inst->osd_layer);
        if (ret != K_SUCCESS) {
            printf("Warning: Failed to disable OSD: 0x%x\n", ret);
        }
    }

    // Configure new OSD attributes
    memset(&osd_attr, 0, sizeof(osd_attr));
    osd_attr.global_alptha   = 0xff; // Fully opaque
    osd_attr.display_rect.x  = 0;
    osd_attr.display_rect.y  = 0;
    osd_attr.img_size.width  = inst->panel_info.resolution.hdisplay;
    osd_attr.img_size.height = inst->panel_info.resolution.vdisplay;
    osd_attr.pixel_format    = inst->pixel_format;

    // Calculate stride based on new pixel format
    switch (inst->pixel_format) {
    case PIXEL_FORMAT_RGB_565:
        osd_attr.stride = inst->panel_info.resolution.hdisplay * 2 / 8;
        break;
    case PIXEL_FORMAT_RGB_888:
        osd_attr.stride = inst->panel_info.resolution.hdisplay * 3 / 8;
        break;
    case PIXEL_FORMAT_ARGB_8888:
        osd_attr.stride = inst->panel_info.resolution.hdisplay * 4 / 8;
        break;
    default:
        printf("Unsupported pixel format for OSD reconfiguration\n");
        return -1;
    }

    ret = kd_mpi_vo_set_video_osd_attr(inst->osd_layer, &osd_attr);
    if (ret != K_SUCCESS) {
        printf("Set new OSD attr failed: 0x%x\n", ret);
        return -1;
    }

    ret = kd_mpi_vo_osd_enable(inst->osd_layer);
    if (ret != K_SUCCESS) {
        printf("Re-enable OSD failed: 0x%x\n", ret);
        return -1;
    }

    inst->layer_configured = 1;
    printf("Successfully reconfigured OSD for new color format\n");
    return 0;
}

static int k230_display_osd_init(lv_k230_display_intstance_t* inst)
{
    k_vo_video_osd_attr osd_attr;
    k_s32               ret;

    if (!inst) {
        return -1;
    }

    // Configure OSD attributes
    memset(&osd_attr, 0, sizeof(osd_attr));
    osd_attr.global_alptha   = 0xff; // Fully opaque
    osd_attr.display_rect.x  = 0;
    osd_attr.display_rect.y  = 0;
    osd_attr.img_size.width  = inst->panel_info.resolution.hdisplay;
    osd_attr.img_size.height = inst->panel_info.resolution.vdisplay;
    osd_attr.pixel_format    = inst->pixel_format;

    // Calculate stride based on pixel format
    switch (inst->pixel_format) {
    case PIXEL_FORMAT_RGB_565:
        osd_attr.stride = inst->panel_info.resolution.hdisplay * 2 / 8;
        break;
    case PIXEL_FORMAT_RGB_888:
        osd_attr.stride = inst->panel_info.resolution.hdisplay * 3 / 8;
        break;
    case PIXEL_FORMAT_ARGB_8888:
        osd_attr.stride = inst->panel_info.resolution.hdisplay * 4 / 8;
        break;
    default:
        printf("Unsupported pixel format for OSD\n");
        return -1;
    }

    ret = kd_mpi_vo_set_video_osd_attr(inst->osd_layer, &osd_attr);
    if (ret != K_SUCCESS) {
        printf("Set OSD attr failed\n");
        return -1;
    }

    ret = kd_mpi_vo_osd_enable(inst->osd_layer);
    if (ret != K_SUCCESS) {
        printf("Enable OSD failed\n");
        return -1;
    }

    inst->layer_configured = 1;
    return 0;
}

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
    if (inst->buffer_display.buffer_addr) {
        kd_mpi_sys_munmap(inst->buffer_display.buffer_addr, inst->buffer_display.buffer_size);
        inst->buffer_display.buffer_addr = NULL;
    }
    if (inst->buffer_display.block_handle != VB_INVALID_HANDLE) {
        kd_mpi_vb_release_block(inst->buffer_display.block_handle);
        inst->buffer_display.block_handle = VB_INVALID_HANDLE;
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

static uint32_t tick_get_cb(void) { return (uint32_t)utils_cpu_ticks_ms(); }

static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* color_p)
{
    lv_k230_display_intstance_t* inst = lv_display_get_driver_data(disp);
    k_s32                        ret;

    if (!inst || !inst->layer_configured) {
        lv_display_flush_ready(disp);
        return;
    }

    // Validate buffer index
    if (inst->current_buffer < 0 || inst->current_buffer >= inst->buffer_count) {
        printf("Invalid buffer index: %d\n", inst->current_buffer);
        lv_display_flush_ready(disp);
        return;
    }

    // Get current buffers
    lv_k230_display_buffer_t* show_buffer = NULL;

    if (LV_DISPLAY_ROTATION_0 == inst->rotation) {
        show_buffer = &inst->buffer[inst->current_buffer];
    } else {
        // Use hardware GDMA rotation for non-0 degree rotations
        if (k230_display_rotate_using_gdma(inst) == 0) {
            show_buffer = &inst->buffer_display;
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

    // Insert frame to OSD layer
    ret = kd_mpi_vo_chn_insert_frame(inst->osd_layer + 3, &show_buffer->vf_info);
    if (ret != K_SUCCESS) {
        printf("Insert frame failed: 0x%x\n", ret);
    }

    // Switch buffer for next flush
    inst->current_buffer = (inst->current_buffer + 1) % inst->buffer_count;

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

            inst->rotation = lv_display_get_rotation(display);
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
            if (k230_display_reconfigure_buffers(inst, new_color_format) != 0) {
                printf("Failed to reconfigure buffers for new color format\n");
                // Rollback to old format
                inst->color_format = old_color_format;
                inst->pixel_format = old_pixel_format;
                lv_display_set_color_format(display, old_color_format);
                // Try to restore OSD with old format
                k230_display_reconfigure_osd(inst);
                break;
            }

            // Reconfigure OSD for new color format
            if (k230_display_reconfigure_osd(inst) != 0) {
                printf("Failed to reconfigure OSD for new color format\n");
                // Rollback: reconfigure buffers back to old format
                inst->color_format = old_color_format;
                inst->pixel_format = old_pixel_format;
                k230_display_reconfigure_buffers(inst, old_color_format);
                lv_display_set_color_format(display, old_color_format);
                // Try to restore OSD with old format
                k230_display_reconfigure_osd(inst);
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
        .gdma_attr.dst_stride[0] = out->v_frame.stride[0],
        .gdma_attr.work_mode     = DMA_UNBIND,
        .gdma_attr.pixel_format  = get_dma_pixel_format(in->v_frame.pixel_format),
    };

    return attr;
}

static int k230_display_rotate_using_gdma(lv_k230_display_intstance_t* inst)
{
    k_s32                     ret;
    k_dma_chn_attr_u          chn_attr;
    k_video_frame_info        tmp_frame;
    k_video_frame_info        src_frame_info; // Temporary source frame with LVGL dimensions
    lv_k230_display_buffer_t* src_buf       = &inst->buffer[inst->current_buffer];
    lv_k230_display_buffer_t* dst_buf       = &inst->buffer_display;
    int                       panel_width   = inst->panel_info.resolution.hdisplay; // 480
    int                       panel_height  = inst->panel_info.resolution.vdisplay; // 800
    int                       rotation_flag = 0;
    int                       lvgl_width, lvgl_height;

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
    lvgl_width  = lv_display_get_horizontal_resolution((lv_display_t*)inst->lvgl_disp);
    lvgl_height = lv_display_get_vertical_resolution((lv_display_t*)inst->lvgl_disp);

    // printf("LVGL resolution: %dx%d, Panel resolution: %dx%d\n", lvgl_width, lvgl_height, panel_width, panel_height);

    switch (inst->rotation) {
    case LV_DISPLAY_ROTATION_90:
        /* LVGL 90° counter-clockwise = DMA 270° clockwise */
        rotation_flag = K_ROTATION_90;
        break;
    case LV_DISPLAY_ROTATION_180:
        rotation_flag = K_ROTATION_180;
        break;
    case LV_DISPLAY_ROTATION_270:
        /* LVGL 270° counter-clockwise = DMA 90° clockwise */
        rotation_flag = K_ROTATION_270;
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
    src_frame_info.v_frame.width        = lvgl_width; // LVGL's rotated width
    src_frame_info.v_frame.height       = lvgl_height; // LVGL's rotated height
    src_frame_info.v_frame.stride[0]    = lvgl_width * bytes_per_pixel;
    src_frame_info.v_frame.pixel_format = inst->pixel_format;
    src_frame_info.v_frame.phys_addr[0] = src_buf->vf_info.v_frame.phys_addr[0];
    src_frame_info.v_frame.priv_data    = K_VO_ONLY_CHANGE_PHYADDR;

    /* Update destination buffer frame info with panel dimensions */
    dst_buf->vf_info.v_frame.width     = panel_width;
    dst_buf->vf_info.v_frame.height    = panel_height;
    dst_buf->vf_info.v_frame.stride[0] = panel_width * bytes_per_pixel;

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
    uint32_t size = dst_buf->vf_info.v_frame.stride[0] * dst_buf->vf_info.v_frame.height * 8;

    // Ensure we don't exceed buffer size
    if (size > dst_buf->buffer_size) {
        size = dst_buf->buffer_size;
    }

    void* tmp_addr = kd_mpi_sys_mmap_cached(tmp_frame.v_frame.phys_addr[0], size);
    if (tmp_addr) {
        // Flush cache and copy data
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
