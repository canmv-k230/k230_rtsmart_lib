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

#include "lv_k230_image_convert.h"

#include "k_type.h"
#include "lv_k230_vglite.h"
#include "lvgl.h"

#include "hal_utils.h"
#include "canmv_misc.h"

#include "mpi_gsdma_api.h"
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"
#include "mpi_vo_api.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef LV_K230_DISPLAY_PRINT_FLUSH_FPS
#define LV_K230_DISPLAY_PRINT_FLUSH_FPS 0
#endif

typedef struct {
    void*              buffer_addr;
    size_t             buffer_size;
    size_t             data_size; // lvgl real used data size
    k_vb_blk_handle    block_handle;
    k_video_frame_info vf_info;
    lv_draw_buf_t      draw_buf;
    bool               sync_rows_valid;
    int32_t            sync_y1;
    int32_t            sync_y2;
} lv_k230_display_buffer_t;

typedef struct {
    k_vo_layer_attr osd_layer_attr;

    lv_color_format_t color_format;

    int                      buffer_count; /* buffer for lvgl count */
    k_u32                    buffer_pool_id; /* buffer pool id */
    lv_k230_display_buffer_t buffer[3]; /* displayed, pending, drawing */
    bool                     buffer_cached;
    lv_k230_display_buffer_t* last_buffer;
    bool                     reconfigure_failed;

    bool    dirty_rows_valid;
    int32_t dirty_y1;
    int32_t dirty_y2;

    lv_display_t* lv_disp;
} lv_k230_display_instance_t;

static uint32_t s_display_count;
static k_s32 k230_display_configure_osd(lv_k230_display_instance_t* inst);

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
        /* LVGL stores 32-bit color as B, G, R, A in memory, which is
         * PIXEL_FORMAT_ARGB_8888 on this little-endian VO interface. */
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
static int k230_display_allocate_single_buffer(lv_k230_display_instance_t* inst, lv_k230_display_buffer_t* buffer,
                                               size_t buffer_size)
{
    buffer->block_handle = kd_mpi_vb_get_block(inst->buffer_pool_id, buffer_size, NULL);
    if (buffer->block_handle == VB_INVALID_HANDLE) {
        printf("Get VB block failed\n");
        return -1;
    }

    buffer->buffer_size                  = buffer_size;
    buffer->vf_info.v_frame.phys_addr[0] = kd_mpi_vb_handle_to_phyaddr(buffer->block_handle);
    buffer->buffer_addr = inst->buffer_cached ? kd_mpi_sys_mmap_cached(buffer->vf_info.v_frame.phys_addr[0], buffer_size)
                                              : kd_mpi_sys_mmap(buffer->vf_info.v_frame.phys_addr[0], buffer_size);

    if (!buffer->buffer_addr) {
        printf("Mmap failed\n");
        kd_mpi_vb_release_block(buffer->block_handle);
        buffer->block_handle = VB_INVALID_HANDLE;
        return -1;
    }

    memset(buffer->buffer_addr, 0, buffer_size);
    if (inst->buffer_cached) {
        kd_mpi_sys_mmz_flush_cache(buffer->vf_info.v_frame.phys_addr[0], buffer->buffer_addr, buffer_size);
    }

    lv_k230_vglite_register_buffer(buffer->buffer_addr, buffer->vf_info.v_frame.phys_addr[0], buffer_size,
                                      inst->buffer_cached);

    return 0;
}

/* Helper function to setup frame information for a buffer */
static void k230_display_setup_frame_info(lv_k230_display_instance_t* inst, lv_k230_display_buffer_t* buffer)
{
    k_u32 layer_width  = inst->osd_layer_attr.img_size.width;
    k_u32 layer_height = inst->osd_layer_attr.img_size.height;

    k_pixel_format pixel_fmt = inst->osd_layer_attr.pixel_format;

    k_u8 pixel_bpp = lv_k230_osd_layer_pixel_fmt_bpp(pixel_fmt);

    buffer->vf_info.mod_id               = K_ID_VO;
    buffer->vf_info.pool_id              = inst->buffer_pool_id;
    buffer->vf_info.v_frame.width        = layer_width;
    buffer->vf_info.v_frame.height       = layer_height;
    buffer->vf_info.v_frame.stride[0]    = lv_draw_buf_width_to_stride(layer_width, inst->color_format);
    if (buffer->vf_info.v_frame.stride[0] == 0) {
        buffer->vf_info.v_frame.stride[0] = layer_width * pixel_bpp;
    }
    buffer->vf_info.v_frame.pixel_format = pixel_fmt;
    buffer->data_size                    = buffer->vf_info.v_frame.stride[0] * layer_height;
    lv_draw_buf_init(&buffer->draw_buf, layer_width, layer_height, inst->color_format,
                     buffer->vf_info.v_frame.stride[0], buffer->buffer_addr, buffer->buffer_size);
}

/* The rest of the functions remain the same... */
static void k230_display_buffer_deinit(lv_k230_display_instance_t* inst)
{
    int i;

    if (!inst) {
        return;
    }

    // Release LVGL buffers
    for (i = 0; i < inst->buffer_count; i++) {
        if (inst->buffer[i].buffer_addr) {
            lv_k230_vglite_unregister_buffer(inst->buffer[i].buffer_addr);
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

static int k230_display_reconfigure(lv_k230_display_instance_t* inst, lv_color_format_t color_format)
{
    int   i;

    size_t buffer_size;

    k_u8  pixel_fmt_bpp;
    k_u32 layer_width, layer_height;
    k_u32 draw_stride;

    if (!inst) {
        return -1;
    }

    layer_width  = inst->osd_layer_attr.img_size.width;
    layer_height = inst->osd_layer_attr.img_size.height;
    pixel_fmt_bpp = lv_k230_map_color_format_to_pixel_size(color_format);

    // Calculate buffer size and stride
    draw_stride = lv_draw_buf_width_to_stride(layer_width, color_format);
    if (draw_stride == 0) {
        draw_stride = layer_width * pixel_fmt_bpp;
    }
    buffer_size = draw_stride * layer_height;
    if (buffer_size == 0) {
        return -1;
    }

    /* Keep the old buffers usable if allocation of the replacement fails. */
    lv_k230_display_instance_t replacement = *inst;
    replacement.color_format = color_format;
    replacement.osd_layer_attr.pixel_format = lv_k230_map_color_format_to_pixel_format(color_format);
    memset(replacement.buffer, 0, sizeof(replacement.buffer));
    for (i = 0; i < inst->buffer_count; i++) {
        replacement.buffer[i].block_handle = VB_INVALID_HANDLE;
    }
    replacement.buffer_cached = !lv_k230_renderer_uses_vglite();

    // Create VB pool for display buffers
    k_vb_remap_mode remap_mode = replacement.buffer_cached ? VB_REMAP_MODE_CACHED : VB_REMAP_MODE_NOCACHE;
    replacement.buffer_pool_id = kd_mpi_vb_create_pool_ex(buffer_size, inst->buffer_count, remap_mode);
    if (replacement.buffer_pool_id == VB_INVALID_POOLID) {
        printf("Failed to create VB pool for display\n");
        return -1;
    }

    // Allocate buffers for LVGL
    for (i = 0; i < inst->buffer_count; i++) {
        if (k230_display_allocate_single_buffer(&replacement, &replacement.buffer[i], buffer_size) != 0) {
            printf("Failed to allocate buffer %d\n", i);
            k230_display_buffer_deinit(&replacement);
            return -1;
        }

        k230_display_setup_frame_info(&replacement, &replacement.buffer[i]);
    }

    /* Keep the old allocation until VO accepts the replacement. Free the
     * candidate before rollback so its memory is available for rotation. */
    if (k230_display_configure_osd(&replacement) != 0) {
        k230_display_buffer_deinit(&replacement);
        if (inst->buffer_pool_id != VB_INVALID_POOLID) {
            if (k230_display_configure_osd(inst) != 0) {
                printf("Failed to restore OSD; display refresh paused\n");
                inst->reconfigure_failed = true;
                lv_timer_pause(lv_display_get_refr_timer(inst->lv_disp));
            } else {
                if (inst->last_buffer) {
                    kd_display_layer_push_frame(inst->osd_layer_attr.layer_id, &inst->last_buffer->vf_info);
                }
                if (inst->reconfigure_failed) {
                    lv_timer_resume(lv_display_get_refr_timer(inst->lv_disp));
                    inst->reconfigure_failed = false;
                }
            }
        }
        return -1;
    }
    if (inst->buffer_pool_id != VB_INVALID_POOLID) {
        k230_display_buffer_deinit(inst);
    }
    inst->color_format = replacement.color_format;
    inst->osd_layer_attr = replacement.osd_layer_attr;
    inst->buffer_pool_id = replacement.buffer_pool_id;
    inst->buffer_cached = replacement.buffer_cached;
    memcpy(inst->buffer, replacement.buffer, sizeof(inst->buffer));
    inst->last_buffer = NULL;

    /* The port chooses a free buffer each refresh. LVGL's built-in triple
     * direct-mode sync also writes the other buffer, which VO may still own. */
    lv_display_set_draw_buffers(inst->lv_disp, &inst->buffer[0].draw_buf, NULL);
    lv_display_set_render_mode(inst->lv_disp, LV_DISPLAY_RENDER_MODE_DIRECT);

    inst->dirty_rows_valid          = false;
    if (inst->reconfigure_failed) {
        lv_timer_resume(lv_display_get_refr_timer(inst->lv_disp));
        inst->reconfigure_failed = false;
    }
    lv_obj_invalidate(lv_display_get_screen_active(inst->lv_disp));

    return 0;
}

static k_s32 k230_display_configure_osd(lv_k230_display_instance_t* inst)
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

static inline lv_k230_display_buffer_t* find_buffer_by_color_p(lv_k230_display_instance_t* inst, uint8_t* color_p)
{
    for (int i = 0; i < inst->buffer_count; i++) {
        lv_k230_display_buffer_t* buff = &inst->buffer[i];

        if (buff->buffer_addr == color_p) {
            return buff;
        }
    }

    return NULL;
}

static void mark_dirty_rows(lv_k230_display_instance_t* inst, const lv_area_t* area)
{
    int32_t y1 = LV_MAX(area->y1, 0);
    int32_t y2 = LV_MIN(area->y2, (int32_t)inst->osd_layer_attr.img_size.height - 1);

    if (y1 > y2) {
        return;
    }

    if (!inst->dirty_rows_valid) {
        inst->dirty_y1         = y1;
        inst->dirty_y2         = y2;
        inst->dirty_rows_valid = true;
        return;
    }

    inst->dirty_y1 = LV_MIN(inst->dirty_y1, y1);
    inst->dirty_y2 = LV_MAX(inst->dirty_y2, y2);
}

static void flush_dirty_rows(lv_k230_display_instance_t* inst, lv_k230_display_buffer_t* buffer)
{
    if (!inst->dirty_rows_valid) {
        return;
    }

    int32_t y1 = inst->dirty_y1;
    int32_t y2 = inst->dirty_y2;

    size_t stride = buffer->vf_info.v_frame.stride[0];
    size_t offset = (size_t)y1 * stride;
    size_t size   = (size_t)(y2 - y1 + 1) * stride;
    kd_mpi_sys_mmz_flush_cache(buffer->vf_info.v_frame.phys_addr[0] + offset,
                               (uint8_t*)buffer->buffer_addr + offset, size);

}

static void prepare_draw_buffer(lv_k230_display_instance_t* inst)
{
    lv_k230_display_buffer_t* next = NULL;

    for (int i = 0; i < inst->buffer_count; i++) {
        lv_k230_display_buffer_t* buffer = &inst->buffer[i];
        if (buffer == inst->last_buffer) {
            continue;
        }
        if (kd_mpi_vo_wait_frame_release(inst->osd_layer_attr.layer_id, &buffer->vf_info, 0) == 0) {
            next = buffer;
            break;
        }
    }

    if (!next) {
        next = inst->last_buffer == &inst->buffer[0] ? &inst->buffer[1] : &inst->buffer[0];
        while (kd_mpi_vo_wait_frame_release(inst->osd_layer_attr.layer_id, &next->vf_info, -1) != 0) {
            if (errno != EINTR) {
                /* Continuing would let LVGL overwrite a buffer owned by VO. */
                perror("VO frame release wait failed");
                abort();
            }
        }
    }

    if (next->sync_rows_valid && inst->last_buffer) {
        lv_area_t area = {0, next->sync_y1,
                         (int32_t)inst->osd_layer_attr.img_size.width - 1, next->sync_y2};
        lv_draw_buf_copy(&next->draw_buf, &area, &inst->last_buffer->draw_buf, &area);
        if (inst->buffer_cached) {
            size_t stride = next->vf_info.v_frame.stride[0];
            size_t offset = (size_t)area.y1 * stride;
            kd_mpi_sys_mmz_flush_cache(next->vf_info.v_frame.phys_addr[0] + offset,
                                       (uint8_t*)next->buffer_addr + offset,
                                       (size_t)(area.y2 - area.y1 + 1) * stride);
        }
        next->sync_rows_valid = false;
    }
    lv_display_set_draw_buffers(inst->lv_disp, &next->draw_buf, NULL);
}

static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* color_p)
{
#if LV_K230_DISPLAY_PRINT_FLUSH_FPS
    static int frame_count = 0;
    static uint64_t last_ticks_ms = 0;
    uint64_t delta, current_ticks_ms;
#endif

    lv_k230_display_instance_t* inst = lv_display_get_driver_data(disp);

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

    mark_dirty_rows(inst, area);

    /*
     * Direct mode can flush several invalid areas from the same draw buffer.
     * Presenting before the last area exposes an incomplete framebuffer and
     * leaves stale regions visible during the buffer swap.
     */
    if (!lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);
        return;
    }

    if (inst->buffer_cached) {
        flush_dirty_rows(inst, current_buffer);
    }

    /*
     * LVGL's draw dispatcher normally drains VG-Lite when it becomes idle.
     * Keep the display boundary explicit as well: VO may latch this buffer at
     * the next refresh immediately after insert_frame returns.
     */
    if (!lv_k230_vglite_wait_idle()) {
        lv_display_flush_ready(disp);
        return;
    }

    if (kd_display_layer_push_frame(inst->osd_layer_attr.layer_id, &current_buffer->vf_info) != K_SUCCESS) {
        printf("Display push frame failed\n");
    }

    /* Remember every change until each spare buffer catches up. The mailbox
     * may drop frames, so a spare can be more than one refresh out of date. */
    for (int i = 0; i < inst->buffer_count; i++) {
        lv_k230_display_buffer_t* buffer = &inst->buffer[i];
        if (buffer == current_buffer || !inst->dirty_rows_valid) continue;
        if (buffer->sync_rows_valid) {
            buffer->sync_y1 = LV_MIN(buffer->sync_y1, inst->dirty_y1);
            buffer->sync_y2 = LV_MAX(buffer->sync_y2, inst->dirty_y2);
        } else {
            buffer->sync_y1 = inst->dirty_y1;
            buffer->sync_y2 = inst->dirty_y2;
            buffer->sync_rows_valid = true;
        }
    }
    inst->last_buffer = current_buffer;
    inst->dirty_rows_valid = false;

    lv_display_flush_ready(disp);

#if LV_K230_DISPLAY_PRINT_FLUSH_FPS
    current_ticks_ms = utils_cpu_ticks_ms();
    if (last_ticks_ms == 0) {
        last_ticks_ms = current_ticks_ms;
    }
    delta = current_ticks_ms - last_ticks_ms;

    frame_count++;

    if (delta >= 1000) { // Update every 1000ms (1 second)
        float elapsed_sec = delta / 1000.0f;
        float avg_fps     = (float)frame_count / elapsed_sec;
        int cpu_usage = 0;

        canmv_misc_get_cpu_usage(&cpu_usage);
        printf("lvgl refresh rate: %.1f fps, cpu: %d\n", avg_fps, cpu_usage);

        frame_count   = 0;
        last_ticks_ms = current_ticks_ms;
    }
#endif
}

static void event_cb(lv_event_t* e)
{
    lv_event_code_t              code    = lv_event_get_code(e);
    lv_display_t*                display = (lv_display_t*)lv_event_get_target(e);
    lv_k230_display_instance_t* inst    = lv_display_get_driver_data(display);

    switch (code) {
    case LV_EVENT_REFR_REQUEST:
        /* LVGL resumes the timer on invalidation, even after OSD rollback fails. */
        if (inst && inst->reconfigure_failed) {
            lv_timer_pause(lv_display_get_refr_timer(display));
        }
        break;
    case LV_EVENT_RENDER_START:
        if (inst) prepare_draw_buffer(inst);
        break;
    case LV_EVENT_DELETE:
        if (inst) {
            // Disable OSD layer
            kd_display_layer_disable(inst->osd_layer_attr.layer_id);

            // Clean up buffers
            k230_display_buffer_deinit(inst);

            lv_display_set_driver_data(display, NULL);
            lv_free(inst);
            if(s_display_count > 0) {
                s_display_count--;
            }
            if(s_display_count == 0) {
                lv_k230_image_convert_deinit();
            }
        }
        break;
    case LV_EVENT_COLOR_FORMAT_CHANGED:
        if (inst) {
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
            if (new_color_format == inst->color_format) {
                printf("Color format unchanged, skipping reconfiguration\n");
                break;
            }

            if (new_pixel_format == old_pixel_format && !inst->reconfigure_failed) {
                inst->color_format = new_color_format;
                for (int i = 0; i < inst->buffer_count; i++) {
                    k230_display_setup_frame_info(inst, &inst->buffer[i]);
                }
                break;
            }

            printf("Reconfiguring display for new color format %d...\n", new_color_format);

            if (k230_display_reconfigure(inst, new_color_format) != 0) {
                printf("Failed to reconfigure display for new color format\n");
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
    k_pixel_format osd_layer_pixel_fmt;

    k_u32 display_width, display_height;

    if ((K_VO_LAYER_OSD0 > layer) || (K_MAX_VO_LAYER_NR <= layer)) {
        printf("invalid layer, only support osd layers\n");

        goto _invalid_osd_layer;
    }

    if (0x00 != kd_display_get_resolution(&display_width, &display_height)) {
        printf("get display resolution failed\n");

        goto _failed_get_resolution;
    }

    lv_k230_display_instance_t* inst = lv_malloc_zeroed(sizeof(lv_k230_display_instance_t));
    lv_display_t*                disp = lv_display_create(display_width, display_height);
    LV_ASSERT_MALLOC(inst && disp);
    if ((NULL == inst) || (NULL == disp)) {
        goto _failed_malloc;
    }

    inst->buffer_count   = 3;
    inst->buffer_pool_id = VB_INVALID_POOLID; /* Will be created internally */
    for (int i = 0; i < inst->buffer_count; i++) {
        inst->buffer[i].block_handle = VB_INVALID_HANDLE;
    }

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
    inst->osd_layer_attr.img_size.width  = display_width;
    inst->osd_layer_attr.img_size.height = display_height;
    inst->osd_layer_attr.pixel_format    = osd_layer_pixel_fmt;
    inst->osd_layer_attr.global_alpha    = alpha;
    /* VO inherits the device rotation selected by kd_display_init(). */
    inst->osd_layer_attr.func            = GDMA_ROTATE_NONE;
    inst->osd_layer_attr.rot_buf_nr      = 2;
    inst->osd_layer_attr.rot_buf_bpp     = 4; // Allow dynamic switching up to 4-byte pixel formats.

    if (0x00 != k230_display_reconfigure(inst, inst->color_format)) {
        printf("Display init failed\n");
        goto _failed_buffer_init;
    }

    int release_result;
    do {
        release_result = kd_mpi_vo_wait_frame_release(layer, &inst->buffer[0].vf_info, 0);
    } while (release_result != 0 && errno == EINTR);
    /* EBUSY also confirms support: another VO operation may hold the lock. */
    if (release_result != 0 && errno != EBUSY) {
        printf("VO frame-release notification is unavailable\n");
        kd_display_layer_disable(layer);
        goto _failed_osd_init;
    }

    lv_display_set_driver_data(disp, inst);

    lv_display_set_flush_cb(disp, flush_cb);

    lv_display_add_event_cb(disp, event_cb, LV_EVENT_COLOR_FORMAT_CHANGED, NULL);
    lv_display_add_event_cb(disp, event_cb, LV_EVENT_DELETE, NULL);
    lv_display_add_event_cb(disp, event_cb, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(disp, event_cb, LV_EVENT_REFR_REQUEST, NULL);

    lv_tick_set_cb(tick_get_cb);

    s_display_count++;
    return disp;

_failed_osd_init:
    k230_display_buffer_deinit(inst);
_failed_buffer_init:
_failed_map_color_format:
_failed_malloc:
    if (disp) {
        lv_display_delete(disp);
    }
    if (inst) {
        lv_free(inst);
    }
_failed_get_resolution:
_invalid_osd_layer:

    return NULL;
}
