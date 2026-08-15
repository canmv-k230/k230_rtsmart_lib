/****************************************************************************
*
*    Copyright 2012 - 2022 Vivante Corporation, Santa Clara, California.
*    All Rights Reserved.
*
*    Permission is hereby granted, free of charge, to any person obtaining
*    a copy of this software and associated documentation files (the
*    'Software'), to deal in the Software without restriction, including
*    without limitation the rights to use, copy, modify, merge, publish,
*    distribute, sub license, and/or sell copies of the Software, and to
*    permit persons to whom the Software is furnished to do so, subject
*    to the following conditions:
*
*    The above copyright notice and this permission notice (including the
*    next paragraph) shall be included in all copies or substantial
*    portions of the Software.
*
*    THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
*    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
*    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
*    IN NO EVENT SHALL VIVANTE AND/OR ITS SUPPLIERS BE LIABLE FOR ANY
*    CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
*    TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
*    SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*
*****************************************************************************/

#include <stdint.h>
#include <stdlib.h>

#define VG_LITE_OS_ALLOC_BINS 5
#define VG_LITE_OS_ACTIVE_BUCKETS 256
#define VG_LITE_OS_BIN_CACHE_LIMIT 1024

static const size_t s_bin_sizes[VG_LITE_OS_ALLOC_BINS] = { 32, 64, 128, 256, 512 };

typedef struct vg_lite_os_alloc_header {
    struct vg_lite_os_alloc_header *next;
    struct vg_lite_os_alloc_header *active_next;
    uint16_t bin;
} vg_lite_os_alloc_header_t;

static vg_lite_os_alloc_header_t *s_free_bins[VG_LITE_OS_ALLOC_BINS];
static uint16_t s_free_bin_counts[VG_LITE_OS_ALLOC_BINS];
static vg_lite_os_alloc_header_t *s_active_allocs[VG_LITE_OS_ACTIVE_BUCKETS];

static int find_bin(size_t size)
{
    int i;

    for (i = 0; i < VG_LITE_OS_ALLOC_BINS; i++) {
        if (size <= s_bin_sizes[i])
            return i;
    }

    return -1;
}

static uint32_t active_bucket(void *memory)
{
    uintptr_t key = (uintptr_t)memory;

    key ^= key >> 16;
    key ^= key >> 8;
    return (uint32_t)key & (VG_LITE_OS_ACTIVE_BUCKETS - 1);
}

static void active_add(vg_lite_os_alloc_header_t *header)
{
    void *memory = (void *)(header + 1);
    uint32_t bucket = active_bucket(memory);

    header->active_next = s_active_allocs[bucket];
    s_active_allocs[bucket] = header;
}

static vg_lite_os_alloc_header_t *active_remove(void *memory)
{
    uint32_t bucket = active_bucket(memory);
    vg_lite_os_alloc_header_t **link = &s_active_allocs[bucket];

    while (*link != NULL) {
        vg_lite_os_alloc_header_t *header = *link;
        if ((void *)(header + 1) == memory) {
            *link = header->active_next;
            header->active_next = NULL;
            return header;
        }
        link = &header->active_next;
    }

    return NULL;
}

void* vg_lite_os_malloc(size_t size)
{
    vg_lite_os_alloc_header_t *header;
    int bin = find_bin(size);

    if (bin < 0)
        return malloc(size);

    header = s_free_bins[bin];
    if (header != NULL) {
        s_free_bins[bin] = header->next;
        s_free_bin_counts[bin]--;
    }
    else {
        header = (vg_lite_os_alloc_header_t *)malloc(sizeof(*header) + s_bin_sizes[bin]);
        if (header == NULL)
            return NULL;
    }

    header->next = NULL;
    header->bin = (uint16_t)bin;
    active_add(header);

    return (void *)(header + 1);
}

void vg_lite_os_free(void* memory)
{
    vg_lite_os_alloc_header_t *header;
    uint16_t bin;

    if (memory == NULL)
        return;

    header = active_remove(memory);
    if (header == NULL) {
        free(memory);
        return;
    }

    bin = header->bin;
    if (bin < VG_LITE_OS_ALLOC_BINS && s_free_bin_counts[bin] < VG_LITE_OS_BIN_CACHE_LIMIT) {
        header->next = s_free_bins[bin];
        s_free_bins[bin] = header;
        s_free_bin_counts[bin]++;
    }
    else {
        free(header);
    }
}
