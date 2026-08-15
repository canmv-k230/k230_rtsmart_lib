/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "lv_k230_image_convert.h"

#include "lv_k230_vglite.h"
#include "lvgl.h"
#include "src/draw/lv_draw_buf_private.h"
#include "src/draw/lv_image_decoder_private.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k_nonai_2d_comm.h"
#include "k_vb_comm.h"
#include "mpi_gsdma_api.h"
#include "mpi_nonai_2d_api.h"
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"

#define CSC_BUFFER_COUNT 2u
#define DECODE_BUFFER_COUNT 3u
#define CSC_MIN_PIXELS (64u * 64u)
#define CSC_TIMEOUT_MS 1000

typedef struct {
    k_u64 phys;
    void * virt;
} csc_buffer_map_t;

typedef struct {
    uint32_t max_width;
    uint32_t max_height;
    uint32_t frame_size;
    k_u32 channel;
    k_u32 pool_id;
    bool channel_requested;
    bool pool_attached;
    bool channel_created;
    bool channel_started;
    bool output_held;
    k_video_frame_info output_frame;
    csc_buffer_map_t maps[CSC_BUFFER_COUNT];
} image_converter_t;

typedef struct {
    lv_draw_buf_t * draw_buf;
    bool in_use;
} decode_buffer_t;

typedef enum {
    MPP_LAYOUT_PACKED,
    MPP_LAYOUT_SEMIPLANAR_420,
    MPP_LAYOUT_PLANAR_420,
} mpp_layout_t;

typedef struct {
    k_pixel_format mpp_format;
    lv_color_format_t lv_format;
    uint8_t bytes_per_pixel;
    bool has_alpha;
    mpp_layout_t layout;
} mpp_format_info_t;

typedef struct {
    const mpp_format_info_t * info;
    k_pixel_format format;
    k_s32 pool_id;
    k_u64 phys[3];
    uint32_t stride[3];
} mpp_source_t;

static pthread_mutex_t s_converter_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t s_decode_lock = PTHREAD_MUTEX_INITIALIZER;
static image_converter_t * s_converter;
static bool s_gsdma_inited;
static bool s_csc_init_failed;
static const char * s_last_backend = "none";
static bool s_copy_needs_flush = true;
static lv_image_decoder_t * s_mpp_decoder;
static decode_buffer_t s_decode_buffers[DECODE_BUFFER_COUNT];

static const mpp_format_info_t s_mpp_formats[] = {
    /* NonAI-2D has no byte-swap mode; non-LE 565 uses the software fallback. */
    {PIXEL_FORMAT_RGB_565,    LV_COLOR_FORMAT_RGB565,   2, false, MPP_LAYOUT_PACKED},
    {PIXEL_FORMAT_BGR_565,    LV_COLOR_FORMAT_RGB565,   2, false, MPP_LAYOUT_PACKED},
    {PIXEL_FORMAT_RGB_565_LE, LV_COLOR_FORMAT_RGB565,   2, false, MPP_LAYOUT_PACKED},
    {PIXEL_FORMAT_BGR_565_LE, LV_COLOR_FORMAT_RGB565,   2, false, MPP_LAYOUT_PACKED},
    {PIXEL_FORMAT_RGB_888,    LV_COLOR_FORMAT_RGB888,   3, false, MPP_LAYOUT_PACKED},
    {PIXEL_FORMAT_BGR_888,    LV_COLOR_FORMAT_RGB888,   3, false, MPP_LAYOUT_PACKED},
    {PIXEL_FORMAT_ARGB_1555,  LV_COLOR_FORMAT_ARGB1555, 2, true,  MPP_LAYOUT_PACKED},
    {PIXEL_FORMAT_ARGB_4444,  LV_COLOR_FORMAT_ARGB4444, 2, true,  MPP_LAYOUT_PACKED},
    {PIXEL_FORMAT_ARGB_8888,  LV_COLOR_FORMAT_ARGB8888, 4, true,  MPP_LAYOUT_PACKED},
    {PIXEL_FORMAT_BGRA_8888,  LV_COLOR_FORMAT_ARGB8888, 4, true,  MPP_LAYOUT_PACKED},
    {PIXEL_FORMAT_YUV_SEMIPLANAR_420, LV_COLOR_FORMAT_NV12, 0, false,
     MPP_LAYOUT_SEMIPLANAR_420},
    {PIXEL_FORMAT_YVU_SEMIPLANAR_420, LV_COLOR_FORMAT_NV21, 0, false,
     MPP_LAYOUT_SEMIPLANAR_420},
    {PIXEL_FORMAT_YVU_PLANAR_420, LV_COLOR_FORMAT_I420, 0, false,
     MPP_LAYOUT_PLANAR_420},
};

static bool csc_convert_full(lv_draw_buf_t * dest, const lv_draw_buf_t * src,
                             uint32_t width, uint32_t height, bool allow_small);

static const mpp_format_info_t * mpp_format_info(k_pixel_format format)
{
    for(size_t i = 0; i < sizeof(s_mpp_formats) / sizeof(s_mpp_formats[0]); i++) {
        if(s_mpp_formats[i].mpp_format == format) {
            return &s_mpp_formats[i];
        }
    }
    return NULL;
}

static bool registered_plane(const void * data, uint32_t size, k_pixel_format expected,
                             k_u64 * phys)
{
    k_pixel_format format;
    return data && size != 0 &&
           lv_k230_vglite_get_mpp_buffer(data, size, phys, &format) &&
           format == expected;
}

static bool resolve_mpp_source(lv_color_format_t color_format, uint32_t width, uint32_t height,
                               uint32_t header_stride, const void * data, uint32_t data_size,
                               mpp_source_t * source)
{
    if(!data || !source || width == 0 || height == 0) {
        return false;
    }

    memset(source, 0, sizeof(*source));
    const void * format_data = data;
    uint32_t format_size = data_size;

    if(color_format == LV_COLOR_FORMAT_NV12 || color_format == LV_COLOR_FORMAT_NV21 ||
       color_format == LV_COLOR_FORMAT_I420) {
        if(data_size < sizeof(lv_yuv_buf_t) || (width & 1u) || (height & 1u)) {
            return false;
        }

        const lv_yuv_buf_t * yuv = data;
        format_data = yuv->planar.y.buf;
        format_size = width * height;
    }

    if(!lv_k230_vglite_get_mpp_buffer(format_data, format_size,
                                      &source->phys[0], &source->format)) {
        return false;
    }

    source->info = mpp_format_info(source->format);
    if(!source->info || source->info->lv_format != color_format) {
        return false;
    }

    if(source->info->layout == MPP_LAYOUT_PACKED) {
        uint32_t stride = header_stride ? header_stride :
                          width * source->info->bytes_per_pixel;
        uint64_t required = (uint64_t)stride * height;
        if(stride != width * source->info->bytes_per_pixel ||
           required == 0 || required > data_size) {
            return false;
        }
        source->stride[0] = stride;
    }
    else {
        const lv_yuv_buf_t * yuv = data;
        uint32_t y_stride = yuv->planar.y.stride ?
                            yuv->planar.y.stride : header_stride;
        if(y_stride != width) {
            return false;
        }
        source->stride[0] = y_stride;

        if(source->info->layout == MPP_LAYOUT_SEMIPLANAR_420) {
            uint32_t uv_stride = yuv->semi_planar.uv.stride;
            uint64_t uv_bytes = (uint64_t)uv_stride * (height / 2u);
            if(uv_stride != width || uv_bytes > UINT32_MAX ||
               !registered_plane(yuv->semi_planar.uv.buf, (uint32_t)uv_bytes,
                                 source->format, &source->phys[1])) {
                return false;
            }
            source->stride[1] = uv_stride;
        }
        else {
            uint32_t u_stride = yuv->planar.u.stride;
            uint32_t v_stride = yuv->planar.v.stride;
            uint64_t plane_bytes = (uint64_t)(width / 2u) * (height / 2u);
            k_u64 u_phys;
            k_u64 v_phys;
            if(u_stride != width / 2u || v_stride != width / 2u ||
               plane_bytes > UINT32_MAX ||
               !registered_plane(yuv->planar.u.buf, (uint32_t)plane_bytes,
                                 source->format, &u_phys) ||
               !registered_plane(yuv->planar.v.buf, (uint32_t)plane_bytes,
                                 source->format, &v_phys)) {
                return false;
            }

            source->phys[1] = u_phys;
            source->phys[2] = v_phys;
            source->stride[1] = u_stride;
            source->stride[2] = v_stride;
        }
    }

    k_vb_blk_handle handle = kd_mpi_vb_phyaddr_to_handle(source->phys[0]);
    source->pool_id = handle == VB_INVALID_HANDLE ?
                      (k_s32)VB_INVALID_POOLID :
                      kd_mpi_vb_handle_to_pool_id(handle);
    return true;
}

static bool mpp_image_format(const lv_image_dsc_t * image, k_pixel_format * format,
                             const mpp_format_info_t ** info)
{
    if(!image || !format || !info || image->header.magic != LV_IMAGE_HEADER_MAGIC ||
       !image->data || image->data_size == 0 ||
       (image->header.flags & LV_IMAGE_FLAGS_COMPRESSED)) {
        return false;
    }

    mpp_source_t source;
    if(!resolve_mpp_source(image->header.cf, image->header.w, image->header.h,
                           image->header.stride, image->data, image->data_size, &source)) {
        return false;
    }

    if(source.pool_id == (k_s32)VB_INVALID_POOLID &&
       source.format != PIXEL_FORMAT_RGB_888 &&
       source.format != PIXEL_FORMAT_BGR_888) {
        return false;
    }

    *format = source.format;
    *info = source.info;
    return true;
}

static decode_buffer_t * decode_buffer_acquire(uint32_t width, uint32_t height,
                                               lv_color_format_t color_format)
{
    pthread_mutex_lock(&s_decode_lock);
    for(uint32_t i = 0; i < DECODE_BUFFER_COUNT; i++) {
        decode_buffer_t * buffer = &s_decode_buffers[i];
        if(buffer->in_use) {
            continue;
        }

        if(buffer->draw_buf &&
           (buffer->draw_buf->header.w != width || buffer->draw_buf->header.h != height ||
            buffer->draw_buf->header.cf != color_format)) {
            lv_draw_buf_destroy(buffer->draw_buf);
            buffer->draw_buf = NULL;
        }

        if(!buffer->draw_buf) {
            buffer->draw_buf = lv_draw_buf_create_ex(lv_draw_buf_get_image_handlers(),
                                                      width, height, color_format,
                                                      LV_STRIDE_AUTO);
            if(!buffer->draw_buf) {
                continue;
            }
        }

        buffer->in_use = true;
        pthread_mutex_unlock(&s_decode_lock);
        return buffer;
    }
    pthread_mutex_unlock(&s_decode_lock);
    return NULL;
}

static void decode_buffer_release(decode_buffer_t * buffer)
{
    if(!buffer) {
        return;
    }

    pthread_mutex_lock(&s_decode_lock);
    buffer->in_use = false;
    pthread_mutex_unlock(&s_decode_lock);
}

static lv_result_t mpp_decoder_info(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc,
                                    lv_image_header_t * header)
{
    LV_UNUSED(decoder);
    if(dsc->src_type != LV_IMAGE_SRC_VARIABLE) {
        return LV_RESULT_INVALID;
    }

    const lv_image_dsc_t * image = dsc->src;
    k_pixel_format format;
    const mpp_format_info_t * info;
    if(!mpp_image_format(image, &format, &info)) {
        return LV_RESULT_INVALID;
    }

    *header = image->header;
    header->cf = info->has_alpha ? LV_COLOR_FORMAT_ARGB8888 : LV_COLOR_FORMAT_XRGB8888;
    header->stride = lv_draw_buf_width_to_stride(header->w, header->cf);
    return LV_RESULT_OK;
}

static lv_result_t mpp_decoder_open(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc)
{
    LV_UNUSED(decoder);
    const lv_image_dsc_t * image = dsc->src;
    k_pixel_format format;
    const mpp_format_info_t * info;
    if(!mpp_image_format(image, &format, &info)) {
        return LV_RESULT_INVALID;
    }

    lv_draw_buf_t source;
    if(info->layout == MPP_LAYOUT_PACKED) {
        uint32_t stride = image->header.stride ? image->header.stride :
                          image->header.w * info->bytes_per_pixel;
        uint64_t required = (uint64_t)stride * image->header.h;
        if(required == 0 || required > image->data_size ||
           lv_draw_buf_init(&source, image->header.w, image->header.h, info->lv_format,
                            stride, (void *)image->data, image->data_size) != LV_RESULT_OK) {
            return LV_RESULT_INVALID;
        }
    }
    else {
        memset(&source, 0, sizeof(source));
        source.header = image->header;
        source.data = (uint8_t *)image->data;
        source.unaligned_data = source.data;
        source.data_size = image->data_size;
        source.handlers = lv_draw_buf_get_image_handlers();
    }
    source.header.flags = image->header.flags;

    decode_buffer_t * buffer = decode_buffer_acquire(image->header.w, image->header.h,
                                                      info->has_alpha ? LV_COLOR_FORMAT_ARGB8888 :
                                                                        LV_COLOR_FORMAT_XRGB8888);
    if(!buffer) {
        return LV_RESULT_INVALID;
    }

    bool converted = csc_convert_full(buffer->draw_buf, &source,
                                      image->header.w, image->header.h, true);
    if(!converted && (format == PIXEL_FORMAT_RGB_888 || format == PIXEL_FORMAT_BGR_888 ||
                      format == PIXEL_FORMAT_RGB_565 || format == PIXEL_FORMAT_BGR_565)) {
        lv_draw_buf_copy(buffer->draw_buf, NULL, &source, NULL);
        converted = true;
    }
    if(!converted) {
        decode_buffer_release(buffer);
        return LV_RESULT_INVALID;
    }
    if(!s_copy_needs_flush) {
        dsc->args.flush_cache = false;
    }
    dsc->header = buffer->draw_buf->header;
    dsc->decoded = buffer->draw_buf;
    dsc->user_data = buffer;
    return LV_RESULT_OK;
}

static void mpp_decoder_close(lv_image_decoder_t * decoder, lv_image_decoder_dsc_t * dsc)
{
    LV_UNUSED(decoder);
    decode_buffer_release(dsc->user_data);
    dsc->user_data = NULL;
    dsc->decoded = NULL;
}

static void mpp_decoder_init(void)
{
    if(s_mpp_decoder) {
        return;
    }

    s_mpp_decoder = lv_image_decoder_create();
    if(!s_mpp_decoder) {
        return;
    }

    lv_image_decoder_set_info_cb(s_mpp_decoder, mpp_decoder_info);
    lv_image_decoder_set_open_cb(s_mpp_decoder, mpp_decoder_open);
    lv_image_decoder_set_close_cb(s_mpp_decoder, mpp_decoder_close);
    s_mpp_decoder->name = "K230 MPP";
}

static void decode_buffers_destroy(void)
{
    pthread_mutex_lock(&s_decode_lock);
    for(uint32_t i = 0; i < DECODE_BUFFER_COUNT; i++) {
        if(s_decode_buffers[i].draw_buf) {
            lv_draw_buf_destroy(s_decode_buffers[i].draw_buf);
            s_decode_buffers[i].draw_buf = NULL;
        }
        s_decode_buffers[i].in_use = false;
    }
    pthread_mutex_unlock(&s_decode_lock);
}

static void * map_output(image_converter_t * converter, k_u64 phys)
{
    int free_index = -1;

    for(uint32_t i = 0; i < CSC_BUFFER_COUNT; i++) {
        if(converter->maps[i].virt && converter->maps[i].phys == phys) {
            return converter->maps[i].virt;
        }
        if(!converter->maps[i].virt && free_index < 0) {
            free_index = (int)i;
        }
    }

    if(free_index < 0) {
        return NULL;
    }

    void * virt = kd_mpi_sys_mmap(phys, converter->frame_size);
    if(!virt) {
        return NULL;
    }

    converter->maps[free_index].phys = phys;
    converter->maps[free_index].virt = virt;
    return virt;
}

static void converter_release_output(image_converter_t * converter)
{
    if(!converter || !converter->output_held) {
        return;
    }

    kd_mpi_nonai_2d_release_frame(converter->channel, &converter->output_frame);
    memset(&converter->output_frame, 0, sizeof(converter->output_frame));
    converter->output_held = false;
}

static void converter_delete(image_converter_t * converter)
{
    if(!converter) {
        return;
    }

    converter_release_output(converter);

    for(uint32_t i = 0; i < CSC_BUFFER_COUNT; i++) {
        if(converter->maps[i].virt) {
            kd_mpi_sys_munmap(converter->maps[i].virt, converter->frame_size);
        }
    }

    if(converter->channel_started) {
        kd_mpi_nonai_2d_stop_chn(converter->channel);
    }
    if(converter->pool_attached) {
        kd_mpi_nonai_2d_detach_vb_pool(converter->channel);
    }
    if(converter->channel_created) {
        kd_mpi_nonai_2d_destroy_chn(converter->channel);
    }
    if(converter->pool_id != VB_INVALID_POOLID) {
        kd_mpi_vb_destory_pool(converter->pool_id);
    }
    if(converter->channel_requested) {
        kd_mpi_nonai_2d_release_chn(converter->channel);
    }

    free(converter);
}

static image_converter_t * converter_create(uint32_t max_width, uint32_t max_height)
{
    if(max_width == 0 || max_height == 0 || max_width > UINT32_MAX / 4u ||
       max_height > UINT32_MAX / (max_width * 4u)) {
        return NULL;
    }

    image_converter_t * converter = calloc(1, sizeof(*converter));
    if(!converter) {
        return NULL;
    }

    converter->max_width = max_width;
    converter->max_height = max_height;
    converter->frame_size = max_width * max_height * 4u;
    converter->pool_id = VB_INVALID_POOLID;

    if(kd_mpi_nonai_2d_request_chn(&converter->channel) != K_SUCCESS) {
        goto fail;
    }
    converter->channel_requested = true;

    k_vb_pool_config pool_config;
    memset(&pool_config, 0, sizeof(pool_config));
    pool_config.blk_cnt = CSC_BUFFER_COUNT;
    pool_config.blk_size = VB_ALIGN_UP(converter->frame_size, 4096);
    pool_config.mode = VB_REMAP_MODE_NOCACHE;
    converter->pool_id = (k_u32)kd_mpi_vb_create_pool(&pool_config);
    if(converter->pool_id == VB_INVALID_POOLID) {
        goto fail;
    }

    if(kd_mpi_nonai_2d_attach_vb_pool(converter->channel, converter->pool_id) != K_SUCCESS) {
        goto fail;
    }
    converter->pool_attached = true;

    k_nonai_2d_chn_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.mode = K_NONAI_2D_CALC_MODE_CSC;
    /* NonAI-2D format names describe byte order. LVGL 32-bit buffers are
     * stored as B, G, R, A/X, so request BGRA output from the converter. */
    attr.dst_fmt = PIXEL_FORMAT_BGRA_8888;
    if(kd_mpi_nonai_2d_create_chn(converter->channel, &attr) != K_SUCCESS) {
        goto fail;
    }
    converter->channel_created = true;

    if(kd_mpi_nonai_2d_start_chn(converter->channel) != K_SUCCESS) {
        goto fail;
    }
    converter->channel_started = true;
    return converter;

fail:
    converter_delete(converter);
    return NULL;
}

static bool ensure_converter(uint32_t width, uint32_t height)
{
    if(s_csc_init_failed) {
        return false;
    }

    if(s_converter && width <= s_converter->max_width && height <= s_converter->max_height) {
        return true;
    }

    uint32_t max_width = s_converter ? LV_MAX(width, s_converter->max_width) : width;
    uint32_t max_height = s_converter ? LV_MAX(height, s_converter->max_height) : height;
    converter_delete(s_converter);
    s_converter = converter_create(max_width, max_height);
    if(!s_converter) {
        s_csc_init_failed = true;
    }
    return s_converter != NULL;
}

static bool converter_run(const k_video_frame_info * source, k_u64 * output_phys, void ** output_virt,
                          uint32_t * output_stride)
{
    image_converter_t * converter = s_converter;
    if(!converter || !source || !output_phys || !output_virt || !output_stride || converter->output_held ||
       source->v_frame.width > converter->max_width || source->v_frame.height > converter->max_height) {
        return false;
    }

    if(kd_mpi_nonai_2d_send_frame(converter->channel, (k_video_frame_info *)source,
                                  CSC_TIMEOUT_MS) != K_SUCCESS) {
        return false;
    }

    k_video_frame_info converted;
    memset(&converted, 0, sizeof(converted));
    if(kd_mpi_nonai_2d_get_frame(converter->channel, &converted, CSC_TIMEOUT_MS) != K_SUCCESS) {
        return false;
    }

    if(converted.v_frame.pixel_format != PIXEL_FORMAT_BGRA_8888 ||
       converted.v_frame.width != source->v_frame.width ||
       converted.v_frame.height != source->v_frame.height) {
        kd_mpi_nonai_2d_release_frame(converter->channel, &converted);
        return false;
    }

    void * virt = map_output(converter, converted.v_frame.phys_addr[0]);
    if(!virt) {
        kd_mpi_nonai_2d_release_frame(converter->channel, &converted);
        return false;
    }

    converter->output_frame = converted;
    converter->output_held = true;
    *output_phys = converted.v_frame.phys_addr[0];
    *output_virt = virt;
    *output_stride = converted.v_frame.stride[0] ?
                     converted.v_frame.stride[0] : source->v_frame.width * 4u;
    return true;
}

static bool get_copy_area(const lv_draw_buf_t * dest, const lv_area_t * dest_area,
                          const lv_draw_buf_t * src, const lv_area_t * src_area,
                          uint32_t * width, uint32_t * height)
{
    uint32_t dest_width = dest_area ? (uint32_t)lv_area_get_width(dest_area) : dest->header.w;
    uint32_t dest_height = dest_area ? (uint32_t)lv_area_get_height(dest_area) : dest->header.h;
    uint32_t src_width = src_area ? (uint32_t)lv_area_get_width(src_area) : src->header.w;
    uint32_t src_height = src_area ? (uint32_t)lv_area_get_height(src_area) : src->header.h;

    if(dest_width == 0 || dest_height == 0 || dest_width != src_width || dest_height != src_height) {
        return false;
    }

    *width = dest_width;
    *height = dest_height;
    return true;
}

static bool copy_same_yuv(lv_draw_buf_t * dest, const lv_area_t * dest_area,
                          const lv_draw_buf_t * src, const lv_area_t * src_area,
                          uint32_t width, uint32_t height)
{
    lv_color_format_t cf = dest->header.cf;
    if(cf != LV_COLOR_FORMAT_NV12 && cf != LV_COLOR_FORMAT_NV21 &&
       cf != LV_COLOR_FORMAT_I420) {
        return false;
    }

    lv_yuv_buf_t * dest_yuv = (lv_yuv_buf_t *)dest->data;
    const lv_yuv_buf_t * src_yuv = (const lv_yuv_buf_t *)src->data;
    uint32_t dest_x = dest_area ? (uint32_t)dest_area->x1 : 0;
    uint32_t dest_y = dest_area ? (uint32_t)dest_area->y1 : 0;
    uint32_t src_x = src_area ? (uint32_t)src_area->x1 : 0;
    uint32_t src_y = src_area ? (uint32_t)src_area->y1 : 0;

    if(!dest_yuv->planar.y.buf || !src_yuv->planar.y.buf) {
        return true;
    }

    uint32_t dest_y_stride = dest_yuv->planar.y.stride;
    uint32_t src_y_stride = src_yuv->planar.y.stride;
    uint8_t * dest_line = (uint8_t *)dest_yuv->planar.y.buf +
                          (uint64_t)dest_y * dest_y_stride + dest_x;
    const uint8_t * src_line = (const uint8_t *)src_yuv->planar.y.buf +
                               (uint64_t)src_y * src_y_stride + src_x;
    for(uint32_t y = 0; y < height; y++) {
        memcpy(dest_line, src_line, width);
        dest_line += dest_y_stride;
        src_line += src_y_stride;
    }

    uint32_t chroma_height = ((dest_y + height + 1u) / 2u) - dest_y / 2u;
    if(cf == LV_COLOR_FORMAT_NV12 || cf == LV_COLOR_FORMAT_NV21) {
        if(!dest_yuv->semi_planar.uv.buf || !src_yuv->semi_planar.uv.buf) {
            return true;
        }

        uint32_t dest_uv_stride = dest_yuv->semi_planar.uv.stride;
        uint32_t src_uv_stride = src_yuv->semi_planar.uv.stride;
        uint32_t dest_uv_x = dest_x & ~1u;
        uint32_t src_uv_x = src_x & ~1u;
        uint32_t uv_bytes = (width + 1u) & ~1u;
        dest_line = (uint8_t *)dest_yuv->semi_planar.uv.buf +
                    (uint64_t)(dest_y / 2u) * dest_uv_stride + dest_uv_x;
        src_line = (const uint8_t *)src_yuv->semi_planar.uv.buf +
                   (uint64_t)(src_y / 2u) * src_uv_stride + src_uv_x;
        for(uint32_t y = 0; y < chroma_height; y++) {
            memcpy(dest_line, src_line, uv_bytes);
            dest_line += dest_uv_stride;
            src_line += src_uv_stride;
        }
        return true;
    }

    if(!dest_yuv->planar.u.buf || !dest_yuv->planar.v.buf ||
       !src_yuv->planar.u.buf || !src_yuv->planar.v.buf) {
        return true;
    }

    uint32_t chroma_width = (width + 1u) / 2u;
    uint32_t dest_chroma_x = dest_x / 2u;
    uint32_t src_chroma_x = src_x / 2u;
    void * dest_planes[] = {dest_yuv->planar.u.buf, dest_yuv->planar.v.buf};
    const void * src_planes[] = {src_yuv->planar.u.buf, src_yuv->planar.v.buf};
    uint32_t dest_strides[] = {dest_yuv->planar.u.stride, dest_yuv->planar.v.stride};
    uint32_t src_strides[] = {src_yuv->planar.u.stride, src_yuv->planar.v.stride};

    for(uint32_t plane = 0; plane < 2; plane++) {
        dest_line = (uint8_t *)dest_planes[plane] +
                    (uint64_t)(dest_y / 2u) * dest_strides[plane] + dest_chroma_x;
        src_line = (const uint8_t *)src_planes[plane] +
                   (uint64_t)(src_y / 2u) * src_strides[plane] + src_chroma_x;
        for(uint32_t y = 0; y < chroma_height; y++) {
            memcpy(dest_line, src_line, chroma_width);
            dest_line += dest_strides[plane];
            src_line += src_strides[plane];
        }
    }
    return true;
}

static void copy_same_format(lv_draw_buf_t * dest, const lv_area_t * dest_area,
                             const lv_draw_buf_t * src, const lv_area_t * src_area,
                             uint32_t width, uint32_t height)
{
    if(copy_same_yuv(dest, dest_area, src, src_area, width, height)) {
        return;
    }

    if((!dest_area || !src_area) && LV_COLOR_FORMAT_IS_INDEXED(dest->header.cf)) {
        size_t palette_bytes = LV_COLOR_INDEXED_PALETTE_SIZE(dest->header.cf) * sizeof(lv_color32_t);
        memcpy(dest->data, src->data, palette_bytes);
    }

    uint8_t * dest_line = lv_draw_buf_goto_xy(dest,
                                               dest_area ? dest_area->x1 : 0,
                                               dest_area ? dest_area->y1 : 0);
    const uint8_t * src_line = lv_draw_buf_goto_xy(src,
                                                   src_area ? src_area->x1 : 0,
                                                   src_area ? src_area->y1 : 0);
    uint32_t line_bytes = (width * lv_color_format_get_bpp(dest->header.cf) + 7u) >> 3;

    for(uint32_t y = 0; y < height; y++) {
        memcpy(dest_line, src_line, line_bytes);
        dest_line += dest->header.stride;
        src_line += src->header.stride;
    }
}

static bool source_mpp_info(const lv_draw_buf_t * src, mpp_source_t * source)
{
    return src && resolve_mpp_source(src->header.cf, src->header.w, src->header.h,
                                     src->header.stride, src->data, src->data_size, source);
}

static bool csc_convert_full(lv_draw_buf_t * dest, const lv_draw_buf_t * src,
                             uint32_t width, uint32_t height, bool allow_small)
{
    if((!allow_small && (uint64_t)width * height < CSC_MIN_PIXELS) ||
       (dest->header.cf != LV_COLOR_FORMAT_XRGB8888 &&
        dest->header.cf != LV_COLOR_FORMAT_ARGB8888)) {
        return false;
    }

    mpp_source_t source;
    if(!source_mpp_info(src, &source) ||
       source.format == PIXEL_FORMAT_RGB_565 || source.format == PIXEL_FORMAT_BGR_565 ||
       source.pool_id == (k_s32)VB_INVALID_POOLID ||
       dest->header.stride != width * 4u) {
        return false;
    }

    k_u64 dest_phys;
    if(!lv_k230_vglite_get_buffer_phys(dest->data, dest->data_size, &dest_phys)) {
        return false;
    }

    pthread_mutex_lock(&s_converter_lock);
    if(!ensure_converter(width, height)) {
        pthread_mutex_unlock(&s_converter_lock);
        return false;
    }

    lv_draw_buf_flush_cache(src, NULL);

    k_video_frame_info source_frame;
    memset(&source_frame, 0, sizeof(source_frame));
    source_frame.pool_id = (k_u32)source.pool_id;
    source_frame.v_frame.width = width;
    source_frame.v_frame.height = height;
    source_frame.v_frame.pixel_format = source.format;
    for(uint32_t i = 0; i < 3; i++) {
        source_frame.v_frame.stride[i] = source.stride[i];
        source_frame.v_frame.phys_addr[i] = source.phys[i];
    }

    k_u64 output_phys;
    void * output_virt;
    uint32_t output_stride;
    bool converted = converter_run(&source_frame, &output_phys, &output_virt, &output_stride);
    if(converted && output_stride == width * 4u) {
        uint64_t bytes = (uint64_t)output_stride * height;
        lv_draw_buf_invalidate_cache(dest, NULL);

        k_sdma_memcpy_t dma;
        dma.dst_phys_addr = dest_phys;
        dma.src_phys_addr = output_phys;
        dma.size = bytes;
        dma.timeout_ms = CSC_TIMEOUT_MS;

        if(!s_gsdma_inited && kd_mpi_gsdma_init() == K_SUCCESS) {
            s_gsdma_inited = true;
        }

        if(s_gsdma_inited && kd_mpi_gsdma_sdma_memcpy(&dma) == K_SUCCESS) {
            lv_draw_buf_invalidate_cache(dest, NULL);
            s_last_backend = "CSC+SDMA";
            s_copy_needs_flush = false;
        }
        else {
            memcpy(dest->data, output_virt, (size_t)bytes);
            s_last_backend = "CSC+memcpy";
            s_copy_needs_flush = true;
        }
    }
    else {
        converted = false;
    }

    converter_release_output(s_converter);
    pthread_mutex_unlock(&s_converter_lock);
    return converted;
}

static uint8_t expand_2_to_8(uint8_t value)
{
    return (uint8_t)(value * 85u);
}

static uint8_t expand_4_to_8(uint8_t value)
{
    return (uint8_t)((value << 4) | value);
}

static uint8_t expand_5_to_8(uint8_t value)
{
    return (uint8_t)((value << 3) | (value >> 2));
}

static uint8_t expand_6_to_8(uint8_t value)
{
    return (uint8_t)((value << 2) | (value >> 4));
}

static bool rvv_expand_to_rgb(uint8_t * dest, uint32_t dest_stride, lv_color_format_t dest_cf,
                             const uint8_t * src, uint32_t src_stride, lv_color_format_t src_cf,
                             uint32_t width, uint32_t height)
{
    if(dest_cf != LV_COLOR_FORMAT_RGB888 &&
       dest_cf != LV_COLOR_FORMAT_XRGB8888 &&
       dest_cf != LV_COLOR_FORMAT_ARGB8888 &&
       dest_cf != LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED) {
        return false;
    }

    bool source_has_alpha = src_cf == LV_COLOR_FORMAT_A8 ||
                            src_cf == LV_COLOR_FORMAT_AL88 ||
                            src_cf == LV_COLOR_FORMAT_ARGB1555 ||
                            src_cf == LV_COLOR_FORMAT_ARGB4444 ||
                            src_cf == LV_COLOR_FORMAT_ARGB2222 ||
                            src_cf == LV_COLOR_FORMAT_ARGB8565;
    if(dest_cf == LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED && source_has_alpha) {
        return false;
    }

    uint32_t src_bytes;
    switch(src_cf) {
        case LV_COLOR_FORMAT_L8:
        case LV_COLOR_FORMAT_A8:
        case LV_COLOR_FORMAT_ARGB2222:
            src_bytes = 1;
            break;
        case LV_COLOR_FORMAT_AL88:
        case LV_COLOR_FORMAT_RGB565:
        case LV_COLOR_FORMAT_RGB565_SWAPPED:
        case LV_COLOR_FORMAT_ARGB1555:
        case LV_COLOR_FORMAT_ARGB4444:
            src_bytes = 2;
            break;
        case LV_COLOR_FORMAT_ARGB8565:
            src_bytes = 3;
            break;
        default:
            return false;
    }

    for(uint32_t y = 0; y < height; y++) {
        size_t remaining = width;
        const uint8_t * src_pixel = src;
        uint8_t * dest_pixel = dest;

        while(remaining > 0) {
            size_t vl;
            asm volatile("vsetvli %0, %1, e8, m1, ta, ma" : "=r"(vl) : "r"(remaining));

            switch(src_cf) {
                case LV_COLOR_FORMAT_L8:
                    asm volatile(
                        "vle8.v v0, (%0)\n\t"
                        "vmv.v.v v4, v0\n\t"
                        "vmv.v.v v5, v0\n\t"
                        "vmv.v.v v6, v0\n\t"
                        "vmv.v.i v7, -1\n\t"
                        :: "r"(src_pixel)
                        : "v0", "v4", "v5", "v6", "v7", "memory");
                    break;
                case LV_COLOR_FORMAT_A8:
                    asm volatile(
                        "vle8.v v7, (%0)\n\t"
                        "vmv.v.i v4, -1\n\t"
                        "vmv.v.i v5, -1\n\t"
                        "vmv.v.i v6, -1\n\t"
                        :: "r"(src_pixel)
                        : "v4", "v5", "v6", "v7", "memory");
                    break;
                case LV_COLOR_FORMAT_AL88:
                    asm volatile(
                        "vlseg2e8.v v0, (%0)\n\t"
                        "vmv.v.v v4, v0\n\t"
                        "vmv.v.v v5, v0\n\t"
                        "vmv.v.v v6, v0\n\t"
                        "vmv.v.v v7, v1\n\t"
                        :: "r"(src_pixel)
                        : "v0", "v1", "v4", "v5", "v6", "v7", "memory");
                    break;
                case LV_COLOR_FORMAT_RGB565:
                case LV_COLOR_FORMAT_RGB565_SWAPPED:
                    asm volatile("vlseg2e8.v v0, (%0)" :: "r"(src_pixel)
                                 : "v0", "v1", "memory");
                    if(src_cf == LV_COLOR_FORMAT_RGB565_SWAPPED) {
                        asm volatile(
                            "vmv.v.v v2, v0\n\t"
                            "vmv.v.v v0, v1\n\t"
                            "vmv.v.v v1, v2\n\t"
                            ::: "v0", "v1", "v2");
                    }
                    asm volatile(
                        "vsll.vi v4, v0, 3\n\t"
                        "vsrl.vi v4, v4, 3\n\t"
                        "vsrl.vi v5, v0, 5\n\t"
                        "vsll.vi v2, v1, 5\n\t"
                        "vsrl.vi v2, v2, 5\n\t"
                        "vsll.vi v2, v2, 3\n\t"
                        "vor.vv v5, v5, v2\n\t"
                        "vsrl.vi v6, v1, 3\n\t"
                        "vmv.v.v v2, v4\n\t"
                        "vsll.vi v4, v4, 3\n\t"
                        "vsrl.vi v2, v2, 2\n\t"
                        "vor.vv v4, v4, v2\n\t"
                        "vmv.v.v v2, v5\n\t"
                        "vsll.vi v5, v5, 2\n\t"
                        "vsrl.vi v2, v2, 4\n\t"
                        "vor.vv v5, v5, v2\n\t"
                        "vmv.v.v v2, v6\n\t"
                        "vsll.vi v6, v6, 3\n\t"
                        "vsrl.vi v2, v2, 2\n\t"
                        "vor.vv v6, v6, v2\n\t"
                        "vmv.v.i v7, -1\n\t"
                        ::: "v2", "v4", "v5", "v6", "v7");
                    break;
                case LV_COLOR_FORMAT_ARGB1555:
                    asm volatile(
                        "vlseg2e8.v v0, (%0)\n\t"
                        "vsrl.vi v6, v0, 1\n\t"
                        "vsll.vi v6, v6, 3\n\t"
                        "vsrl.vi v6, v6, 3\n\t"
                        "vsrl.vi v5, v0, 6\n\t"
                        "vsll.vi v2, v1, 5\n\t"
                        "vsrl.vi v2, v2, 5\n\t"
                        "vsll.vi v2, v2, 2\n\t"
                        "vor.vv v5, v5, v2\n\t"
                        "vsrl.vi v4, v1, 3\n\t"
                        "vsll.vi v7, v0, 7\n\t"
                        "vsrl.vi v7, v7, 7\n\t"
                        "vmul.vx v7, v7, %1\n\t"
                        "vmv.v.v v2, v4\n\t"
                        "vsll.vi v4, v4, 3\n\t"
                        "vsrl.vi v2, v2, 2\n\t"
                        "vor.vv v4, v4, v2\n\t"
                        "vmv.v.v v2, v5\n\t"
                        "vsll.vi v5, v5, 3\n\t"
                        "vsrl.vi v2, v2, 2\n\t"
                        "vor.vv v5, v5, v2\n\t"
                        "vmv.v.v v2, v6\n\t"
                        "vsll.vi v6, v6, 3\n\t"
                        "vsrl.vi v2, v2, 2\n\t"
                        "vor.vv v6, v6, v2\n\t"
                        :: "r"(src_pixel), "r"(255u)
                        : "v0", "v1", "v2", "v4", "v5", "v6", "v7", "memory");
                    break;
                case LV_COLOR_FORMAT_ARGB4444:
                    asm volatile(
                        "vlseg2e8.v v0, (%0)\n\t"
                        "vsrl.vi v4, v1, 4\n\t"
                        "vsll.vi v5, v1, 4\n\t"
                        "vsrl.vi v5, v5, 4\n\t"
                        "vsrl.vi v6, v0, 4\n\t"
                        "vsll.vi v7, v0, 4\n\t"
                        "vsrl.vi v7, v7, 4\n\t"
                        "vmv.v.v v2, v4\n\t"
                        "vsll.vi v4, v4, 4\n\t"
                        "vor.vv v4, v4, v2\n\t"
                        "vmv.v.v v2, v5\n\t"
                        "vsll.vi v5, v5, 4\n\t"
                        "vor.vv v5, v5, v2\n\t"
                        "vmv.v.v v2, v6\n\t"
                        "vsll.vi v6, v6, 4\n\t"
                        "vor.vv v6, v6, v2\n\t"
                        "vmv.v.v v2, v7\n\t"
                        "vsll.vi v7, v7, 4\n\t"
                        "vor.vv v7, v7, v2\n\t"
                        :: "r"(src_pixel)
                        : "v0", "v1", "v2", "v4", "v5", "v6", "v7", "memory");
                    break;
                case LV_COLOR_FORMAT_ARGB2222:
                    asm volatile(
                        "vle8.v v0, (%0)\n\t"
                        "vsrl.vi v4, v0, 6\n\t"
                        "vsll.vi v5, v0, 2\n\t"
                        "vsrl.vi v5, v5, 6\n\t"
                        "vsll.vi v6, v0, 4\n\t"
                        "vsrl.vi v6, v6, 6\n\t"
                        "vsll.vi v7, v0, 6\n\t"
                        "vsrl.vi v7, v7, 6\n\t"
                        "vmul.vx v4, v4, %1\n\t"
                        "vmul.vx v5, v5, %1\n\t"
                        "vmul.vx v6, v6, %1\n\t"
                        "vmul.vx v7, v7, %1\n\t"
                        :: "r"(src_pixel), "r"(85u)
                        : "v0", "v4", "v5", "v6", "v7", "memory");
                    break;
                case LV_COLOR_FORMAT_ARGB8565:
                    asm volatile(
                        "vlseg3e8.v v0, (%0)\n\t"
                        "vmv.v.v v7, v2\n\t"
                        "vsll.vi v4, v0, 3\n\t"
                        "vsrl.vi v4, v4, 3\n\t"
                        "vsrl.vi v5, v0, 5\n\t"
                        "vsll.vi v3, v1, 5\n\t"
                        "vsrl.vi v3, v3, 5\n\t"
                        "vsll.vi v3, v3, 3\n\t"
                        "vor.vv v5, v5, v3\n\t"
                        "vsrl.vi v6, v1, 3\n\t"
                        "vmv.v.v v3, v4\n\t"
                        "vsll.vi v4, v4, 3\n\t"
                        "vsrl.vi v3, v3, 2\n\t"
                        "vor.vv v4, v4, v3\n\t"
                        "vmv.v.v v3, v5\n\t"
                        "vsll.vi v5, v5, 2\n\t"
                        "vsrl.vi v3, v3, 4\n\t"
                        "vor.vv v5, v5, v3\n\t"
                        "vmv.v.v v3, v6\n\t"
                        "vsll.vi v6, v6, 3\n\t"
                        "vsrl.vi v3, v3, 2\n\t"
                        "vor.vv v6, v6, v3\n\t"
                        :: "r"(src_pixel)
                        : "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "memory");
                    break;
                default:
                    return false;
            }

            if(dest_cf == LV_COLOR_FORMAT_RGB888) {
                asm volatile("vsseg3e8.v v4, (%0)" :: "r"(dest_pixel) : "memory");
            }
            else {
                if(dest_cf == LV_COLOR_FORMAT_XRGB8888) {
                    asm volatile("vmv.v.i v7, -1" ::: "v7");
                }
                asm volatile("vsseg4e8.v v4, (%0)" :: "r"(dest_pixel) : "memory");
            }

            src_pixel += vl * src_bytes;
            dest_pixel += vl * (dest_cf == LV_COLOR_FORMAT_RGB888 ? 3u : 4u);
            remaining -= vl;
        }

        src += src_stride;
        dest += dest_stride;
    }
    return true;
}

static bool rvv_pack_from_24_32(uint8_t * dest, uint32_t dest_stride,
                                lv_color_format_t dest_cf, const uint8_t * src,
                                uint32_t src_stride, lv_color_format_t src_cf,
                                uint32_t width, uint32_t height, bool source_is_rgb)
{
    uint32_t src_bytes;
    switch(src_cf) {
        case LV_COLOR_FORMAT_RGB888:
            src_bytes = 3;
            break;
        case LV_COLOR_FORMAT_XRGB8888:
        case LV_COLOR_FORMAT_ARGB8888:
            src_bytes = 4;
            break;
        default:
            return false;
    }

    uint32_t dest_bytes;
    switch(dest_cf) {
        case LV_COLOR_FORMAT_RGB565:
        case LV_COLOR_FORMAT_RGB565_SWAPPED:
        case LV_COLOR_FORMAT_ARGB1555:
        case LV_COLOR_FORMAT_ARGB4444:
            dest_bytes = 2;
            break;
        case LV_COLOR_FORMAT_ARGB2222:
            dest_bytes = 1;
            break;
        case LV_COLOR_FORMAT_ARGB8565:
            dest_bytes = 3;
            break;
        default:
            return false;
    }

    for(uint32_t y = 0; y < height; y++) {
        size_t remaining = width;
        const uint8_t * src_pixel = src;
        uint8_t * dest_pixel = dest;

        while(remaining > 0) {
            size_t vl;
            asm volatile("vsetvli %0, %1, e8, m1, ta, ma" : "=r"(vl) : "r"(remaining));
            if(src_bytes == 3u) {
                if(source_is_rgb) {
                    asm volatile(
                        "vlseg3e8.v v0, (%0)\n\t"
                        "vmv.v.v v4, v2\n\t"
                        "vmv.v.v v5, v1\n\t"
                        "vmv.v.v v6, v0\n\t"
                        "vmv.v.i v7, -1\n\t"
                        :: "r"(src_pixel)
                        : "v0", "v1", "v2", "v4", "v5", "v6", "v7", "memory");
                }
                else {
                    asm volatile(
                        "vlseg3e8.v v0, (%0)\n\t"
                        "vmv.v.v v4, v0\n\t"
                        "vmv.v.v v5, v1\n\t"
                        "vmv.v.v v6, v2\n\t"
                        "vmv.v.i v7, -1\n\t"
                        :: "r"(src_pixel)
                        : "v0", "v1", "v2", "v4", "v5", "v6", "v7", "memory");
                }
            }
            else {
                asm volatile(
                    "vlseg4e8.v v0, (%0)\n\t"
                    "vmv.v.v v4, v0\n\t"
                    "vmv.v.v v5, v1\n\t"
                    "vmv.v.v v6, v2\n\t"
                    "vmv.v.v v7, v3\n\t"
                    :: "r"(src_pixel)
                    : "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "memory");
                if(src_cf == LV_COLOR_FORMAT_XRGB8888) {
                    asm volatile("vmv.v.i v7, -1" ::: "v7");
                }
            }

            switch(dest_cf) {
                case LV_COLOR_FORMAT_RGB565:
                case LV_COLOR_FORMAT_RGB565_SWAPPED:
                case LV_COLOR_FORMAT_ARGB8565:
                    asm volatile(
                        "vsrl.vi v0, v4, 3\n\t"
                        "vsrl.vi v2, v5, 2\n\t"
                        "vsll.vi v2, v2, 5\n\t"
                        "vor.vv v0, v0, v2\n\t"
                        "vsrl.vi v1, v5, 5\n\t"
                        "vsrl.vi v2, v6, 3\n\t"
                        "vsll.vi v2, v2, 3\n\t"
                        "vor.vv v1, v1, v2\n\t"
                        ::: "v0", "v1", "v2");
                    if(dest_cf == LV_COLOR_FORMAT_RGB565_SWAPPED) {
                        asm volatile(
                            "vmv.v.v v2, v0\n\t"
                            "vmv.v.v v0, v1\n\t"
                            "vmv.v.v v1, v2\n\t"
                            ::: "v0", "v1", "v2");
                    }
                    if(dest_cf == LV_COLOR_FORMAT_ARGB8565) {
                        asm volatile(
                            "vmv.v.v v2, v7\n\t"
                            "vsseg3e8.v v0, (%0)\n\t"
                            :: "r"(dest_pixel) : "v2", "memory");
                    }
                    else {
                        asm volatile("vsseg2e8.v v0, (%0)" :: "r"(dest_pixel) : "memory");
                    }
                    break;
                case LV_COLOR_FORMAT_ARGB1555:
                    asm volatile(
                        "vsrl.vi v0, v7, 7\n\t"
                        "vsrl.vi v2, v6, 3\n\t"
                        "vsll.vi v2, v2, 1\n\t"
                        "vor.vv v0, v0, v2\n\t"
                        "vsrl.vi v2, v5, 3\n\t"
                        "vsll.vi v2, v2, 6\n\t"
                        "vor.vv v0, v0, v2\n\t"
                        "vsrl.vi v1, v5, 5\n\t"
                        "vsrl.vi v2, v4, 3\n\t"
                        "vsll.vi v2, v2, 3\n\t"
                        "vor.vv v1, v1, v2\n\t"
                        "vsseg2e8.v v0, (%0)\n\t"
                        :: "r"(dest_pixel) : "v0", "v1", "v2", "memory");
                    break;
                case LV_COLOR_FORMAT_ARGB4444:
                    asm volatile(
                        "vsrl.vi v0, v7, 4\n\t"
                        "vsrl.vi v2, v6, 4\n\t"
                        "vsll.vi v2, v2, 4\n\t"
                        "vor.vv v0, v0, v2\n\t"
                        "vsrl.vi v1, v5, 4\n\t"
                        "vsrl.vi v2, v4, 4\n\t"
                        "vsll.vi v2, v2, 4\n\t"
                        "vor.vv v1, v1, v2\n\t"
                        "vsseg2e8.v v0, (%0)\n\t"
                        :: "r"(dest_pixel) : "v0", "v1", "v2", "memory");
                    break;
                case LV_COLOR_FORMAT_ARGB2222:
                    asm volatile(
                        "vsrl.vi v0, v4, 6\n\t"
                        "vsll.vi v0, v0, 6\n\t"
                        "vsrl.vi v2, v5, 6\n\t"
                        "vsll.vi v2, v2, 4\n\t"
                        "vor.vv v0, v0, v2\n\t"
                        "vsrl.vi v2, v6, 6\n\t"
                        "vsll.vi v2, v2, 2\n\t"
                        "vor.vv v0, v0, v2\n\t"
                        "vsrl.vi v2, v7, 6\n\t"
                        "vor.vv v0, v0, v2\n\t"
                        "vse8.v v0, (%0)\n\t"
                        :: "r"(dest_pixel) : "v0", "v2", "memory");
                    break;
                default:
                    return false;
            }

            src_pixel += vl * src_bytes;
            dest_pixel += vl * dest_bytes;
            remaining -= vl;
        }

        src += src_stride;
        dest += dest_stride;
    }
    return true;
}

static bool rvv_packed_format(lv_color_format_t cf, uint32_t * bytes)
{
    switch(cf) {
        case LV_COLOR_FORMAT_RGB888:
            *bytes = 3;
            return true;
        case LV_COLOR_FORMAT_XRGB8888:
        case LV_COLOR_FORMAT_ARGB8888:
        case LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED:
            *bytes = 4;
            return true;
        default:
            return false;
    }
}

static bool rvv_convert_packed(uint8_t * dest, uint32_t dest_stride, lv_color_format_t dest_cf,
                               const uint8_t * src, uint32_t src_stride, lv_color_format_t src_cf,
                               uint32_t width, uint32_t height, bool source_is_rgb)
{
    uint32_t src_bytes;
    uint32_t dest_bytes;
    if(!rvv_packed_format(src_cf, &src_bytes) || !rvv_packed_format(dest_cf, &dest_bytes)) {
        return false;
    }

    bool src_premultiplied = src_cf == LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED;
    bool dest_premultiplied = dest_cf == LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED;
    bool source_opaque = src_cf == LV_COLOR_FORMAT_RGB888 || src_cf == LV_COLOR_FORMAT_XRGB8888;
    if(src_premultiplied != dest_premultiplied && !source_opaque) {
        return false;
    }

    /* K230 always provides RVV, and RT-Smart lazily enables vector state for
     * user threads when their first vector instruction traps into the kernel. */
    for(uint32_t y = 0; y < height; y++) {
        size_t remaining = width;
        uint8_t * dest_pixel = dest;
        const uint8_t * src_pixel = src;

        while(remaining > 0) {
            size_t vl;
            asm volatile("vsetvli %0, %1, e8, m1, ta, ma" : "=r"(vl) : "r"(remaining));

            if(src_bytes == 3u) {
                if(source_is_rgb) {
                    asm volatile(
                        "vlseg3e8.v v0, (%0)\n\t"
                        "vmv.v.v v4, v2\n\t"
                        "vmv.v.v v5, v1\n\t"
                        "vmv.v.v v6, v0\n\t"
                        "vmv.v.i v7, -1\n\t"
                        :
                        : "r"(src_pixel)
                        : "v0", "v1", "v2", "v4", "v5", "v6", "v7", "memory");
                }
                else {
                    asm volatile(
                        "vlseg3e8.v v0, (%0)\n\t"
                        "vmv.v.v v4, v0\n\t"
                        "vmv.v.v v5, v1\n\t"
                        "vmv.v.v v6, v2\n\t"
                        "vmv.v.i v7, -1\n\t"
                        :
                        : "r"(src_pixel)
                        : "v0", "v1", "v2", "v4", "v5", "v6", "v7", "memory");
                }
            }
            else {
                asm volatile(
                    "vlseg4e8.v v0, (%0)\n\t"
                    "vmv.v.v v4, v0\n\t"
                    "vmv.v.v v5, v1\n\t"
                    "vmv.v.v v6, v2\n\t"
                    "vmv.v.v v7, v3\n\t"
                    :
                    : "r"(src_pixel)
                    : "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "memory");
                if(src_cf == LV_COLOR_FORMAT_XRGB8888) {
                    asm volatile("vmv.v.i v7, -1" ::: "v7");
                }
            }

            if(dest_bytes == 3u) {
                asm volatile("vsseg3e8.v v4, (%0)" :: "r"(dest_pixel) : "memory");
            }
            else {
                if(dest_cf == LV_COLOR_FORMAT_XRGB8888) {
                    asm volatile("vmv.v.i v7, -1" ::: "v7");
                }
                asm volatile("vsseg4e8.v v4, (%0)" :: "r"(dest_pixel) : "memory");
            }

            src_pixel += vl * src_bytes;
            dest_pixel += vl * dest_bytes;
            remaining -= vl;
        }
        src += src_stride;
        dest += dest_stride;
    }
    return true;
}

static bool read_pixel(const uint8_t * src, lv_color_format_t cf,
                       uint8_t * blue, uint8_t * green, uint8_t * red, uint8_t * alpha)
{
    uint16_t value;

    switch(cf) {
        case LV_COLOR_FORMAT_L8:
            *blue = src[0];
            *green = src[0];
            *red = src[0];
            *alpha = 0xffu;
            return true;
        case LV_COLOR_FORMAT_A8:
            *blue = 0xffu;
            *green = 0xffu;
            *red = 0xffu;
            *alpha = src[0];
            return true;
        case LV_COLOR_FORMAT_AL88:
            *blue = src[0];
            *green = src[0];
            *red = src[0];
            *alpha = src[1];
            return true;
        case LV_COLOR_FORMAT_RGB565:
        case LV_COLOR_FORMAT_RGB565_SWAPPED:
            memcpy(&value, src, sizeof(value));
            if(cf == LV_COLOR_FORMAT_RGB565_SWAPPED) {
                value = (uint16_t)((value << 8) | (value >> 8));
            }
            *blue = expand_5_to_8(value & 0x1fu);
            *green = expand_6_to_8((value >> 5) & 0x3fu);
            *red = expand_5_to_8((value >> 11) & 0x1fu);
            *alpha = 0xffu;
            return true;
        case LV_COLOR_FORMAT_RGB888:
            *blue = src[0];
            *green = src[1];
            *red = src[2];
            *alpha = 0xffu;
            return true;
        case LV_COLOR_FORMAT_XRGB8888:
            *blue = src[0];
            *green = src[1];
            *red = src[2];
            *alpha = 0xffu;
            return true;
        case LV_COLOR_FORMAT_ARGB8888:
        case LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED:
            *blue = src[0];
            *green = src[1];
            *red = src[2];
            *alpha = src[3];
            if(cf == LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED && *alpha != 0u && *alpha != 0xffu) {
                *blue = (uint8_t)LV_MIN(255u, ((uint32_t)*blue * 255u + *alpha / 2u) / *alpha);
                *green = (uint8_t)LV_MIN(255u, ((uint32_t)*green * 255u + *alpha / 2u) / *alpha);
                *red = (uint8_t)LV_MIN(255u, ((uint32_t)*red * 255u + *alpha / 2u) / *alpha);
            }
            return true;
        case LV_COLOR_FORMAT_ARGB1555:
            memcpy(&value, src, sizeof(value));
            *blue = expand_5_to_8((value >> 11) & 0x1fu);
            *green = expand_5_to_8((value >> 6) & 0x1fu);
            *red = expand_5_to_8((value >> 1) & 0x1fu);
            *alpha = (value & 1u) ? 0xffu : 0u;
            return true;
        case LV_COLOR_FORMAT_ARGB4444:
            memcpy(&value, src, sizeof(value));
            *blue = expand_4_to_8((value >> 12) & 0x0fu);
            *green = expand_4_to_8((value >> 8) & 0x0fu);
            *red = expand_4_to_8((value >> 4) & 0x0fu);
            *alpha = expand_4_to_8(value & 0x0fu);
            return true;
        case LV_COLOR_FORMAT_ARGB2222:
            *blue = expand_2_to_8((src[0] >> 6) & 0x03u);
            *green = expand_2_to_8((src[0] >> 4) & 0x03u);
            *red = expand_2_to_8((src[0] >> 2) & 0x03u);
            *alpha = expand_2_to_8(src[0] & 0x03u);
            return true;
        case LV_COLOR_FORMAT_ARGB8565:
            memcpy(&value, src, sizeof(value));
            *blue = expand_5_to_8(value & 0x1fu);
            *green = expand_6_to_8((value >> 5) & 0x3fu);
            *red = expand_5_to_8((value >> 11) & 0x1fu);
            *alpha = src[2];
            return true;
        default:
            return false;
    }
}

static uint8_t luminance(uint8_t blue, uint8_t green, uint8_t red)
{
    return (uint8_t)(((uint32_t)red * 77u + (uint32_t)green * 150u +
                      (uint32_t)blue * 29u + 128u) >> 8);
}

static bool write_pixel(uint8_t * dest, lv_color_format_t cf,
                        uint8_t blue, uint8_t green, uint8_t red, uint8_t alpha)
{
    uint16_t value;

    switch(cf) {
        case LV_COLOR_FORMAT_L8:
            dest[0] = luminance(blue, green, red);
            return true;
        case LV_COLOR_FORMAT_A8:
            dest[0] = alpha;
            return true;
        case LV_COLOR_FORMAT_AL88:
            dest[0] = luminance(blue, green, red);
            dest[1] = alpha;
            return true;
        case LV_COLOR_FORMAT_RGB565:
        case LV_COLOR_FORMAT_RGB565_SWAPPED:
            value = (uint16_t)(((uint16_t)(red >> 3) << 11) |
                               ((uint16_t)(green >> 2) << 5) |
                               (blue >> 3));
            if(cf == LV_COLOR_FORMAT_RGB565_SWAPPED) {
                value = (uint16_t)((value << 8) | (value >> 8));
            }
            memcpy(dest, &value, sizeof(value));
            return true;
        case LV_COLOR_FORMAT_RGB888:
            dest[0] = blue;
            dest[1] = green;
            dest[2] = red;
            return true;
        case LV_COLOR_FORMAT_XRGB8888:
            dest[0] = blue;
            dest[1] = green;
            dest[2] = red;
            dest[3] = 0xffu;
            return true;
        case LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED:
            blue = (uint8_t)(((uint32_t)blue * alpha + 127u) / 255u);
            green = (uint8_t)(((uint32_t)green * alpha + 127u) / 255u);
            red = (uint8_t)(((uint32_t)red * alpha + 127u) / 255u);
            /* fall through */
        case LV_COLOR_FORMAT_ARGB8888:
            dest[0] = blue;
            dest[1] = green;
            dest[2] = red;
            dest[3] = alpha;
            return true;
        case LV_COLOR_FORMAT_ARGB1555:
            value = (uint16_t)(((uint16_t)(blue >> 3) << 11) |
                               ((uint16_t)(green >> 3) << 6) |
                               ((uint16_t)(red >> 3) << 1) |
                               (alpha >> 7));
            memcpy(dest, &value, sizeof(value));
            return true;
        case LV_COLOR_FORMAT_ARGB4444:
            value = (uint16_t)(((uint16_t)(blue >> 4) << 12) |
                               ((uint16_t)(green >> 4) << 8) |
                               ((uint16_t)(red >> 4) << 4) |
                               (alpha >> 4));
            memcpy(dest, &value, sizeof(value));
            return true;
        case LV_COLOR_FORMAT_ARGB2222:
            dest[0] = (uint8_t)((blue & 0xc0u) | ((green >> 2) & 0x30u) |
                                ((red >> 4) & 0x0cu) | (alpha >> 6));
            return true;
        case LV_COLOR_FORMAT_ARGB8565:
            value = (uint16_t)(((uint16_t)(red >> 3) << 11) |
                               ((uint16_t)(green >> 2) << 5) |
                               (blue >> 3));
            memcpy(dest, &value, sizeof(value));
            dest[2] = alpha;
            return true;
        default:
            return false;
    }
}

static bool scalar_convert(uint8_t * dest, uint32_t dest_stride, lv_color_format_t dest_cf,
                           const uint8_t * src, uint32_t src_stride, lv_color_format_t src_cf,
                           uint32_t width, uint32_t height, bool source_is_rgb)
{
    uint32_t src_bytes = lv_color_format_get_size(src_cf);
    uint32_t dest_bytes = lv_color_format_get_size(dest_cf);
    if(src_bytes == 0 || dest_bytes == 0) {
        return false;
    }

    for(uint32_t y = 0; y < height; y++) {
        for(uint32_t x = 0; x < width; x++) {
            uint8_t blue;
            uint8_t green;
            uint8_t red;
            uint8_t alpha;
            if(!read_pixel(src + x * src_bytes, src_cf, &blue, &green, &red, &alpha)) {
                return false;
            }
            if(source_is_rgb && src_cf == LV_COLOR_FORMAT_RGB888) {
                uint8_t swap = blue;
                blue = red;
                red = swap;
            }
            if(!write_pixel(dest + x * dest_bytes, dest_cf, blue, green, red, alpha)) {
                return false;
            }
        }
        src += src_stride;
        dest += dest_stride;
    }
    return true;
}

static bool source_mpp_format(const lv_draw_buf_t * src, k_pixel_format * format)
{
    k_u64 phys;
    return src && format &&
           lv_k230_vglite_get_mpp_buffer(src->data, src->data_size, &phys, format);
}

static bool scalar_convert_mpp_565(uint8_t * dest, uint32_t dest_stride,
                                   lv_color_format_t dest_cf, const uint8_t * src,
                                   uint32_t src_stride, k_pixel_format src_format,
                                   uint32_t width, uint32_t height)
{
    if(src_format != PIXEL_FORMAT_RGB_565 && src_format != PIXEL_FORMAT_BGR_565) {
        return false;
    }

    uint32_t dest_bytes = lv_color_format_get_size(dest_cf);
    if(dest_bytes == 0) {
        return false;
    }

    for(uint32_t y = 0; y < height; y++) {
        for(uint32_t x = 0; x < width; x++) {
            const uint8_t * src_pixel = src + x * 2u;
            uint16_t value = (uint16_t)(((uint16_t)src_pixel[0] << 8) | src_pixel[1]);
            uint8_t high = expand_5_to_8((uint8_t)((value >> 11) & 0x1fu));
            uint8_t green = expand_6_to_8((uint8_t)((value >> 5) & 0x3fu));
            uint8_t low = expand_5_to_8((uint8_t)(value & 0x1fu));
            uint8_t red = src_format == PIXEL_FORMAT_BGR_565 ? low : high;
            uint8_t blue = src_format == PIXEL_FORMAT_BGR_565 ? high : low;
            if(!write_pixel(dest + x * dest_bytes, dest_cf, blue, green, red, 0xffu)) {
                return false;
            }
        }
        src += src_stride;
        dest += dest_stride;
    }
    return true;
}

static bool source_uses_rgb_byte_order(const lv_draw_buf_t * src)
{
    k_pixel_format format;
    return src->header.cf == LV_COLOR_FORMAT_RGB888 &&
           source_mpp_format(src, &format) && format == PIXEL_FORMAT_RGB_888;
}

static void draw_buf_copy(lv_draw_buf_t * dest, const lv_area_t * dest_area,
                          const lv_draw_buf_t * src, const lv_area_t * src_area)
{
    uint32_t width;
    uint32_t height;
    s_copy_needs_flush = true;
    if(!get_copy_area(dest, dest_area, src, src_area, &width, &height)) {
        LV_LOG_ERROR("K230 draw-buffer copy area mismatch");
        return;
    }

    k_pixel_format registered_format = PIXEL_FORMAT_BUTT;
    bool source_registered = source_mpp_format(src, &registered_format);
    bool source_is_rgb = source_uses_rgb_byte_order(src);
    bool source_is_nonle_565 = source_registered &&
                               (registered_format == PIXEL_FORMAT_RGB_565 ||
                                registered_format == PIXEL_FORMAT_BGR_565);
    bool full_copy = !dest_area && !src_area &&
                     width == dest->header.w && width == src->header.w &&
                     height == dest->header.h && height == src->header.h;
    if(full_copy && csc_convert_full(dest, src, width, height, false)) {
        return;
    }

    if(dest->header.cf == src->header.cf && !source_is_rgb && !source_is_nonle_565) {
        copy_same_format(dest, dest_area, src, src_area, width, height);
        return;
    }

    uint8_t * dest_data = lv_draw_buf_goto_xy(dest,
                                               dest_area ? dest_area->x1 : 0,
                                               dest_area ? dest_area->y1 : 0);
    const uint8_t * src_data = lv_draw_buf_goto_xy(src,
                                                    src_area ? src_area->x1 : 0,
                                                    src_area ? src_area->y1 : 0);

    if(source_is_nonle_565 &&
       scalar_convert_mpp_565(dest_data, dest->header.stride, dest->header.cf,
                              src_data, src->header.stride, registered_format,
                              width, height)) {
        s_last_backend = "scalar";
        return;
    }

    if(rvv_expand_to_rgb(dest_data, dest->header.stride, dest->header.cf,
                        src_data, src->header.stride, src->header.cf,
                        width, height)) {
        s_last_backend = "RVV";
        return;
    }

    if(rvv_pack_from_24_32(dest_data, dest->header.stride, dest->header.cf,
                           src_data, src->header.stride, src->header.cf,
                           width, height, source_is_rgb)) {
        s_last_backend = "RVV";
        return;
    }

    if(rvv_convert_packed(dest_data, dest->header.stride, dest->header.cf,
                          src_data, src->header.stride, src->header.cf,
                          width, height, source_is_rgb)) {
        s_last_backend = "RVV";
        return;
    }

    if(scalar_convert(dest_data, dest->header.stride, dest->header.cf,
                      src_data, src->header.stride, src->header.cf, width, height,
                      source_is_rgb)) {
        s_last_backend = "scalar";
        return;
    }

    LV_LOG_ERROR("Unsupported K230 color conversion: %d -> %d",
                 src->header.cf, dest->header.cf);
}

void lv_k230_image_convert_init(void)
{
    mpp_decoder_init();

    lv_draw_buf_handlers_t * handlers[] = {
        lv_draw_buf_get_handlers(),
        lv_draw_buf_get_font_handlers(),
        lv_draw_buf_get_image_handlers(),
    };

    for(size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++) {
        handlers[i]->buf_copy_cb = draw_buf_copy;
    }
}

void lv_k230_image_convert_deinit(void)
{
    pthread_mutex_lock(&s_converter_lock);
    converter_delete(s_converter);
    s_converter = NULL;
    s_csc_init_failed = false;
    if(s_gsdma_inited) {
        kd_mpi_gsdma_deinit();
        s_gsdma_inited = false;
    }
    s_last_backend = "none";
    s_copy_needs_flush = true;
    pthread_mutex_unlock(&s_converter_lock);

    decode_buffers_destroy();
}

const char * lv_k230_image_convert_backend(void)
{
    return s_last_backend;
}
