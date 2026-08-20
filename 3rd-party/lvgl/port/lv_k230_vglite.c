/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "lv_k230_vglite.h"

#include "lv_k230_image_convert.h"

#include "lvgl.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if LV_USE_DRAW_VG_LITE
#include <pthread.h>

#include "k_mmz_comm.h"
#include "mpi_sys_api.h"
#include "src/draw/lv_draw_buf_private.h"
#include "vg_lite.h"

#if LV_USE_OS != LV_OS_NONE
#error "The LVGL VG-Lite backend requires LV_OS_NONE"
#endif

typedef struct lv_k230_vglite_buffer_node {
    void * virt;
    k_u64 phys;
    size_t size;
    void * vg_handle;
    uint32_t vg_address;
    bool cached;
    bool owned;
    bool map_failed;
    bool mpp_format_valid;
    k_pixel_format mpp_format;
    struct lv_k230_vglite_buffer_node * next;
} lv_k230_vglite_buffer_node_t;

typedef struct {
    const void * virt;
    uint32_t size;
    uint32_t address;
} lv_k230_vglite_address_cache_t;

#define ADDRESS_CACHE_COUNT 256u

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static lv_k230_vglite_buffer_node_t * s_buffers;
static lv_k230_vglite_address_cache_t s_address_cache[ADDRESS_CACHE_COUNT];
static bool s_gpu_inited;

static bool range_in_node(const lv_k230_vglite_buffer_node_t * node, const void * ptr, size_t size,
                          size_t * offset_out)
{
    uintptr_t start = (uintptr_t)node->virt;
    uintptr_t value = (uintptr_t)ptr;

    if(value < start) {
        return false;
    }

    size_t offset = value - start;
    if(offset >= node->size || (size != 0 && size > node->size - offset)) {
        return false;
    }

    if(offset_out) {
        *offset_out = offset;
    }
    return true;
}

static lv_k230_vglite_buffer_node_t * find_node_locked(const void * ptr, size_t size, size_t * offset)
{
    for(lv_k230_vglite_buffer_node_t * node = s_buffers; node; node = node->next) {
        if(range_in_node(node, ptr, size, offset)) {
            return node;
        }
    }

    return NULL;
}

static size_t address_cache_index(const void * ptr, uint32_t size)
{
    uintptr_t key = (uintptr_t)ptr;
    key ^= key >> 17;
    key ^= key >> 31;
    key ^= (uintptr_t)size * 2654435761u;
    return key & (ADDRESS_CACHE_COUNT - 1u);
}

static bool address_cache_lookup_locked(const void * ptr, uint32_t size, uint32_t * address)
{
    lv_k230_vglite_address_cache_t * entry = &s_address_cache[address_cache_index(ptr, size)];
    if(entry->virt != ptr || entry->size != size) {
        return false;
    }

    *address = entry->address;
    return true;
}

static void address_cache_store_locked(const void * ptr, uint32_t size, uint32_t address)
{
    lv_k230_vglite_address_cache_t * entry = &s_address_cache[address_cache_index(ptr, size)];
    entry->virt = ptr;
    entry->size = size;
    entry->address = address;
}

static void address_cache_clear_locked(void)
{
    lv_memzero(s_address_cache, sizeof(s_address_cache));
}

static void unmap_node(lv_k230_vglite_buffer_node_t * node)
{
    if(!node->vg_handle) {
        return;
    }

    vg_lite_buffer_t buffer;
    lv_memzero(&buffer, sizeof(buffer));
    buffer.handle = node->vg_handle;
    buffer.memory = node->virt;
    buffer.address = node->vg_address;

    vg_lite_error_t err = vg_lite_unmap(&buffer);
    if(err != VG_LITE_SUCCESS) {
        printf("LVGL VG-Lite unmap failed: %d\n", (int)err);
    }
    node->vg_handle = NULL;
}

static bool map_node_locked(lv_k230_vglite_buffer_node_t * node)
{
    if(node->vg_handle) {
        return true;
    }
    if(node->map_failed || !s_gpu_inited || node->size == 0 || node->size > INT32_MAX ||
       node->phys > UINT32_MAX || node->size - 1u > UINT32_MAX - (uint32_t)node->phys) {
        return false;
    }

    vg_lite_buffer_t buffer;
    lv_memzero(&buffer, sizeof(buffer));
    buffer.width = (int32_t)node->size;
    buffer.height = 1;
    buffer.stride = (int32_t)node->size;
    buffer.format = VG_LITE_A8;
    buffer.memory = node->virt;
    buffer.address = (uint32_t)node->phys;

    vg_lite_error_t err = vg_lite_map(&buffer, VG_LITE_MAP_USER_MEMORY, -1);
    if(err != VG_LITE_SUCCESS) {
        node->map_failed = true;
        printf("LVGL VG-Lite map failed for %p (%zu bytes): %d\n", node->virt, node->size, (int)err);
        return false;
    }

    node->vg_handle = buffer.handle;
    node->vg_address = buffer.address;
    return true;
}

static void release_node(lv_k230_vglite_buffer_node_t * node)
{
    unmap_node(node);
    if(node->owned) {
        kd_mpi_sys_mmz_free(node->phys, node->virt);
    }
    free(node);
}

void lv_k230_vglite_register_buffer(void * virt, k_u64 phys, size_t size, bool cached)
{
    if(!virt || size == 0) {
        return;
    }

    lv_k230_vglite_buffer_node_t * replacement = calloc(1, sizeof(*replacement));
    if(!replacement) {
        return;
    }

    replacement->virt = virt;
    replacement->phys = phys;
    replacement->size = size;
    replacement->cached = cached;

    pthread_mutex_lock(&s_lock);
    address_cache_clear_locked();
    lv_k230_vglite_buffer_node_t ** link = &s_buffers;
    while(*link && (*link)->virt != virt) {
        link = &(*link)->next;
    }

    lv_k230_vglite_buffer_node_t * old = *link;
    if(old && (old->owned || (old->phys == phys && old->size == size && old->cached == cached))) {
        pthread_mutex_unlock(&s_lock);
        free(replacement);
        return;
    }

    replacement->next = old ? old->next : s_buffers;
    if(old) {
        *link = replacement;
    }
    else {
        s_buffers = replacement;
    }
    pthread_mutex_unlock(&s_lock);

    if(old) {
        release_node(old);
    }
}

void lv_k230_vglite_register_mpp_buffer(void * virt, k_u64 phys, size_t size, bool cached,
                                          k_pixel_format format)
{
    lv_k230_vglite_register_buffer(virt, phys, size, cached);

    pthread_mutex_lock(&s_lock);
    size_t offset;
    lv_k230_vglite_buffer_node_t * node = find_node_locked(virt, size, &offset);
    if(node && offset == 0 && node->phys == phys) {
        node->mpp_format = format;
        node->mpp_format_valid = true;
    }
    pthread_mutex_unlock(&s_lock);
}

bool lv_k230_vglite_get_mpp_buffer(const void * ptr, size_t size, k_u64 * phys, k_pixel_format * format)
{
    if(!ptr || !phys || !format) {
        return false;
    }

    pthread_mutex_lock(&s_lock);
    size_t offset;
    lv_k230_vglite_buffer_node_t * node = find_node_locked(ptr, size, &offset);
    if(!node || !node->mpp_format_valid) {
        pthread_mutex_unlock(&s_lock);
        return false;
    }

    *phys = node->phys + offset;
    *format = node->mpp_format;
    pthread_mutex_unlock(&s_lock);
    return true;
}

void lv_k230_vglite_unregister_buffer(void * virt)
{
    if(!virt) {
        return;
    }

    pthread_mutex_lock(&s_lock);
    address_cache_clear_locked();
    lv_k230_vglite_buffer_node_t ** link = &s_buffers;
    while(*link) {
        lv_k230_vglite_buffer_node_t * node = *link;
        if(node->virt == virt && !node->owned) {
            *link = node->next;
            pthread_mutex_unlock(&s_lock);
            release_node(node);
            return;
        }
        link = &node->next;
    }
    pthread_mutex_unlock(&s_lock);
}

static void * draw_buf_malloc(size_t size, lv_color_format_t color_format)
{
    LV_UNUSED(color_format);

    if(size == 0) {
        return NULL;
    }

    uint64_t allocation_size64 = (uint64_t)size + LV_DRAW_BUF_ALIGN - 1u;
    allocation_size64 = (allocation_size64 + 63u) & ~(uint64_t)63u;
    if(allocation_size64 > UINT32_MAX) {
        return NULL;
    }
    size_t allocation_size = (size_t)allocation_size64;

    k_u64 phys = 0;
    void * virt = NULL;
    if(kd_mpi_sys_mmz_alloc_cached(&phys, &virt, "lvgl_vglite", NULL, (k_u32)allocation_size) != 0) {
        return NULL;
    }

    lv_k230_vglite_buffer_node_t * node = calloc(1, sizeof(*node));
    if(!node) {
        kd_mpi_sys_mmz_free(phys, virt);
        return NULL;
    }

    node->virt = virt;
    node->phys = phys;
    node->size = allocation_size;
    node->cached = true;
    node->owned = true;

    pthread_mutex_lock(&s_lock);
    node->next = s_buffers;
    s_buffers = node;
    pthread_mutex_unlock(&s_lock);

    return virt;
}

static void draw_buf_free(void * buf)
{
    if(!buf) {
        return;
    }

    pthread_mutex_lock(&s_lock);
    lv_k230_vglite_buffer_node_t ** link = &s_buffers;
    while(*link) {
        lv_k230_vglite_buffer_node_t * node = *link;
        if(node->virt == buf && node->owned) {
            *link = node->next;
            pthread_mutex_unlock(&s_lock);
            release_node(node);
            return;
        }
        link = &node->next;
    }
    pthread_mutex_unlock(&s_lock);
}

bool lv_vg_lite_port_get_buffer_addr(const void * ptr, uint32_t size, uintptr_t * addr)
{
    if(!ptr || !addr) {
        return false;
    }

    pthread_mutex_lock(&s_lock);
    size_t offset;
    lv_k230_vglite_buffer_node_t * node = find_node_locked(ptr, size, &offset);
    if(node && node->phys <= UINT32_MAX && offset <= UINT32_MAX - (uint32_t)node->phys) {
        *addr = (uintptr_t)(node->phys + offset);
        pthread_mutex_unlock(&s_lock);
        return true;
    }
    uint32_t query_size = size ? size : 1u;
    uint32_t address;
    uint32_t cacheable;
    if(address_cache_lookup_locked(ptr, query_size, &address)) {
        *addr = address;
        pthread_mutex_unlock(&s_lock);
        return true;
    }
    pthread_mutex_unlock(&s_lock);

    if(vg_lite_get_memory_address(ptr, query_size, &address, &cacheable) != VG_LITE_SUCCESS) {
        return false;
    }
    if(cacheable && vg_lite_clean_memory(ptr, query_size) != VG_LITE_SUCCESS) {
        return false;
    }

    pthread_mutex_lock(&s_lock);
    address_cache_store_locked(ptr, query_size, address);
    pthread_mutex_unlock(&s_lock);
    *addr = address;
    return true;
}

bool lv_k230_vglite_get_buffer_phys(const void * ptr, size_t size, k_u64 * phys)
{
    if(!phys || size > UINT32_MAX) {
        return false;
    }

    uintptr_t address;
    if(!lv_vg_lite_port_get_buffer_addr(ptr, (uint32_t)size, &address)) {
        return false;
    }

    *phys = (k_u64)address;
    return true;
}

bool lv_vg_lite_port_draw_buf_is_gpu_accessible(const lv_draw_buf_t * draw_buf)
{
    if(!draw_buf || !draw_buf->data || draw_buf->data_size == 0) {
        return false;
    }

    if(draw_buf->header.cf == LV_COLOR_FORMAT_NV12) {
        const lv_yuv_buf_t * frame = (const lv_yuv_buf_t *)draw_buf->data;
        uint64_t y_bytes = (uint64_t)draw_buf->header.stride * draw_buf->header.h;
        uint64_t uv_bytes = (uint64_t)frame->semi_planar.uv.stride * (draw_buf->header.h / 2u);
        uintptr_t address;

        if(!frame->semi_planar.y.buf || !frame->semi_planar.uv.buf ||
           y_bytes == 0 || uv_bytes == 0 || y_bytes > UINT32_MAX || uv_bytes > UINT32_MAX) {
            return false;
        }

        return lv_vg_lite_port_get_buffer_addr(frame->semi_planar.y.buf, (uint32_t)y_bytes, &address) &&
               lv_vg_lite_port_get_buffer_addr(frame->semi_planar.uv.buf, (uint32_t)uv_bytes, &address);
    }

    uintptr_t address;
    return lv_vg_lite_port_get_buffer_addr(draw_buf->data, draw_buf->data_size, &address);
}

bool lv_vg_lite_port_map_buffer(vg_lite_buffer_t * buffer)
{
    if(!buffer || !buffer->memory || buffer->stride <= 0 || buffer->height <= 0) {
        return false;
    }

    uint64_t bytes64 = (uint64_t)(uint32_t)buffer->stride * (uint32_t)buffer->height;
    if(bytes64 == 0 || bytes64 > UINT32_MAX) {
        return false;
    }

    bool mapped = false;
    bool registered = false;
    pthread_mutex_lock(&s_lock);
    size_t offset;
    lv_k230_vglite_buffer_node_t * node = find_node_locked(buffer->memory, (size_t)bytes64, &offset);
    registered = node != NULL;
    if(node && map_node_locked(node) && offset <= UINT32_MAX - node->vg_address) {
        buffer->handle = node->vg_handle;
        buffer->address = node->vg_address + (uint32_t)offset;
        mapped = true;
    }
    pthread_mutex_unlock(&s_lock);

    if(mapped || registered) {
        return mapped;
    }

    uintptr_t address;
    if(!lv_vg_lite_port_get_buffer_addr(buffer->memory, (uint32_t)bytes64, &address) ||
       address > UINT32_MAX) {
        return false;
    }

    /* This descriptor is transient, so there is no owner that could unmap a
     * handle. Rendering supports a physical address without a handle. */
    buffer->handle = NULL;
    buffer->address = (uint32_t)address;
    return true;
}

static bool cache_operation(const void * ptr, uint32_t size, bool invalidate)
{
    if(!ptr || size == 0) {
        return false;
    }

    pthread_mutex_lock(&s_lock);
    size_t offset;
    lv_k230_vglite_buffer_node_t * node = find_node_locked(ptr, 0, &offset);
    if(node) {
        if(size > node->size - offset) {
            size = (uint32_t)(node->size - offset);
        }
        if(!node->cached) {
            pthread_mutex_unlock(&s_lock);
            return true;
        }

        k_u64 phys = node->phys + offset;
        int result = invalidate ? kd_mpi_sys_mmz_invalidate_cache(phys, (void *)ptr, size)
                     : kd_mpi_sys_mmz_flush_cache(phys, (void *)ptr, size);
        pthread_mutex_unlock(&s_lock);
        return result == 0;
    }
    pthread_mutex_unlock(&s_lock);

    vg_lite_error_t result = invalidate ? vg_lite_invalidate_memory(ptr, size)
                             : vg_lite_clean_memory(ptr, size);
    return result == VG_LITE_SUCCESS;
}

static bool yuv_cache_operation(const lv_draw_buf_t * draw_buf, const lv_area_t * area, bool invalidate)
{
    const lv_yuv_buf_t * frame = (const lv_yuv_buf_t *)draw_buf->data;
    int32_t y1 = LV_MAX(area->y1, 0);
    int32_t y2 = LV_MIN(area->y2, (int32_t)draw_buf->header.h - 1);
    if(y1 > y2 || !frame->planar.y.buf) {
        return false;
    }

    uint32_t y_stride = frame->planar.y.stride ?
                        frame->planar.y.stride : draw_buf->header.stride;
    uint64_t y_offset = (uint64_t)(uint32_t)y1 * y_stride;
    uint64_t y_bytes = (uint64_t)(uint32_t)(y2 - y1 + 1) * y_stride;
    if(y_bytes == 0 || y_bytes > UINT32_MAX || y_offset > SIZE_MAX ||
       !cache_operation((const uint8_t *)frame->planar.y.buf + (size_t)y_offset,
                        (uint32_t)y_bytes, invalidate)) {
        return false;
    }

    uint32_t chroma_y1 = (uint32_t)y1 / 2u;
    uint32_t chroma_y2 = (uint32_t)y2 / 2u;
    if(draw_buf->header.cf == LV_COLOR_FORMAT_NV12 ||
       draw_buf->header.cf == LV_COLOR_FORMAT_NV21) {
        uint32_t uv_stride = frame->semi_planar.uv.stride;
        uint64_t uv_offset = (uint64_t)chroma_y1 * uv_stride;
        uint64_t uv_bytes = (uint64_t)(chroma_y2 - chroma_y1 + 1u) * uv_stride;
        return frame->semi_planar.uv.buf && uv_bytes != 0 && uv_bytes <= UINT32_MAX &&
               uv_offset <= SIZE_MAX &&
               cache_operation((const uint8_t *)frame->semi_planar.uv.buf + (size_t)uv_offset,
                               (uint32_t)uv_bytes, invalidate);
    }

    if(draw_buf->header.cf == LV_COLOR_FORMAT_I420) {
        uint32_t u_stride = frame->planar.u.stride;
        uint32_t v_stride = frame->planar.v.stride;
        uint64_t u_offset = (uint64_t)chroma_y1 * u_stride;
        uint64_t v_offset = (uint64_t)chroma_y1 * v_stride;
        uint64_t u_bytes = (uint64_t)(chroma_y2 - chroma_y1 + 1u) * u_stride;
        uint64_t v_bytes = (uint64_t)(chroma_y2 - chroma_y1 + 1u) * v_stride;
        return frame->planar.u.buf && frame->planar.v.buf &&
               u_bytes != 0 && v_bytes != 0 &&
               u_bytes <= UINT32_MAX && v_bytes <= UINT32_MAX &&
               u_offset <= SIZE_MAX && v_offset <= SIZE_MAX &&
               cache_operation((const uint8_t *)frame->planar.u.buf + (size_t)u_offset,
                               (uint32_t)u_bytes, invalidate) &&
               cache_operation((const uint8_t *)frame->planar.v.buf + (size_t)v_offset,
                               (uint32_t)v_bytes, invalidate);
    }

    return false;
}

static bool draw_buf_cache_region(const lv_draw_buf_t * draw_buf, const lv_area_t * area,
                                  const void ** ptr, uint32_t * size)
{
    if(!draw_buf || !draw_buf->data || !area || draw_buf->data_size == 0) {
        return false;
    }

    if(LV_COLOR_FORMAT_IS_INDEXED(draw_buf->header.cf) ||
       draw_buf->header.cf == LV_COLOR_FORMAT_RGB565A8) {
        *ptr = draw_buf->data;
        *size = draw_buf->data_size;
        return true;
    }

    int32_t y1 = LV_MAX(area->y1, 0);
    int32_t y2 = LV_MIN(area->y2, (int32_t)draw_buf->header.h - 1);
    if(y1 > y2) {
        return false;
    }

    uint64_t offset = (uint64_t)(uint32_t)y1 * draw_buf->header.stride;
    uint64_t bytes = (uint64_t)(uint32_t)(y2 - y1 + 1) * draw_buf->header.stride;
    if(offset >= draw_buf->data_size) {
        return false;
    }
    if(bytes > draw_buf->data_size - offset) {
        bytes = draw_buf->data_size - offset;
    }

    *ptr = draw_buf->data + (size_t)offset;
    *size = (uint32_t)bytes;
    return bytes != 0;
}

static void draw_buf_flush_cache(const lv_draw_buf_t * draw_buf, const lv_area_t * area)
{
    if(draw_buf->header.cf == LV_COLOR_FORMAT_NV12 ||
       draw_buf->header.cf == LV_COLOR_FORMAT_NV21 ||
       draw_buf->header.cf == LV_COLOR_FORMAT_I420) {
        yuv_cache_operation(draw_buf, area, false);
        return;
    }

    const void * ptr;
    uint32_t size;
    if(draw_buf_cache_region(draw_buf, area, &ptr, &size)) {
        cache_operation(ptr, size, false);
    }
}

static void draw_buf_invalidate_cache(const lv_draw_buf_t * draw_buf, const lv_area_t * area)
{
    if(draw_buf->header.cf == LV_COLOR_FORMAT_NV12 ||
       draw_buf->header.cf == LV_COLOR_FORMAT_NV21 ||
       draw_buf->header.cf == LV_COLOR_FORMAT_I420) {
        yuv_cache_operation(draw_buf, area, true);
        return;
    }

    const void * ptr;
    uint32_t size;
    if(draw_buf_cache_region(draw_buf, area, &ptr, &size)) {
        cache_operation(ptr, size, true);
    }
}

static void init_draw_buf_handlers(void)
{
    lv_draw_buf_handlers_t * handlers[] = {
        lv_draw_buf_get_handlers(),
        lv_draw_buf_get_font_handlers(),
        lv_draw_buf_get_image_handlers(),
    };

    for(size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++) {
        handlers[i]->buf_malloc_cb = draw_buf_malloc;
        handlers[i]->buf_free_cb = draw_buf_free;
        handlers[i]->invalidate_cache_cb = draw_buf_invalidate_cache;
        handlers[i]->flush_cache_cb = draw_buf_flush_cache;
    }

    lv_k230_image_convert_init();
}

void lv_vg_lite_port_init_draw_buf_handlers(void)
{
    init_draw_buf_handlers();
}

bool gpu_init(void)
{
    if(s_gpu_inited) {
        return true;
    }

    init_draw_buf_handlers();

    vg_lite_error_t err = vg_lite_init(CONFIG_RTSMART_3RD_PARTY_LVGL_VGLITE_TESS_WIDTH,
                                       CONFIG_RTSMART_3RD_PARTY_LVGL_VGLITE_TESS_HEIGHT);
    if(err == VG_LITE_SUCCESS) {
        s_gpu_inited = true;
        printf("LVGL VG-Lite init: tess=%dx%d\n",
               CONFIG_RTSMART_3RD_PARTY_LVGL_VGLITE_TESS_WIDTH,
               CONFIG_RTSMART_3RD_PARTY_LVGL_VGLITE_TESS_HEIGHT);
    }
    else {
        printf("LVGL VG-Lite init failed: %d\n", (int)err);
    }

    return s_gpu_inited;
}

bool lv_k230_vglite_wait_idle(void)
{
    if(!s_gpu_inited) {
        return true;
    }

    vg_lite_error_t err = vg_lite_finish();
    if(err != VG_LITE_SUCCESS) {
        printf("LVGL VG-Lite finish failed: %d\n", (int)err);
        return false;
    }

    return true;
}

const char * lv_k230_vglite_renderer_name(void)
{
    return "vg_lite+sw";
}

#else

void lv_k230_vglite_register_buffer(void * virt, k_u64 phys, size_t size, bool cached)
{
    LV_UNUSED(virt);
    LV_UNUSED(phys);
    LV_UNUSED(size);
    LV_UNUSED(cached);
}

void lv_k230_vglite_register_mpp_buffer(void * virt, k_u64 phys, size_t size, bool cached,
                                          k_pixel_format format)
{
    LV_UNUSED(virt);
    LV_UNUSED(phys);
    LV_UNUSED(size);
    LV_UNUSED(cached);
    LV_UNUSED(format);
}

bool lv_k230_vglite_get_mpp_buffer(const void * ptr, size_t size, k_u64 * phys, k_pixel_format * format)
{
    LV_UNUSED(ptr);
    LV_UNUSED(size);
    LV_UNUSED(phys);
    LV_UNUSED(format);
    return false;
}

void lv_k230_vglite_unregister_buffer(void * virt)
{
    LV_UNUSED(virt);
}

bool lv_k230_vglite_get_buffer_phys(const void * ptr, size_t size, k_u64 * phys)
{
    LV_UNUSED(ptr);
    LV_UNUSED(size);
    LV_UNUSED(phys);
    return false;
}

bool lv_k230_vglite_wait_idle(void)
{
    return true;
}

const char * lv_k230_vglite_renderer_name(void)
{
    return "sw";
}

#endif
