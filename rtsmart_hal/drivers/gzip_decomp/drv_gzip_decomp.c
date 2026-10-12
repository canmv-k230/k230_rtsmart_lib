/* Copyright (c) 2026, Canaan Bright Sight Co., Ltd
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "drv_gzip_decomp.h"
#include "hal_rvv_ops.h"
#include "mpi_sys_api.h"

#define RT_GZIP_DECOMP_GUNZIP _IOWR('Z', 0, int)

#define DRV_GZIP_DECOMP_DEVICE_PATH "/dev/gzip_decomp"

#define DRV_GZIP_DECOMP_DEFAULT_SRC_SIZE (256 * 1024)
#define DRV_GZIP_DECOMP_DEFAULT_DST_SIZE (1 * 1024 * 1024)

/* The UGZIP engine exchanges data with SDMA in 128KB linked-list nodes; the
 * last node of a transfer still spans a full 128KB, so DMA may touch up to
 * one node beyond the payload. Buffers handed to the engine must be padded
 * to this granularity. */
#define DRV_GZIP_DECOMP_NODE_SIZE (128 * 1024)

static uint32_t _drv_gzip_decomp_align_node(uint32_t size)
{
    return (size + DRV_GZIP_DECOMP_NODE_SIZE - 1) & ~(uint32_t)(DRV_GZIP_DECOMP_NODE_SIZE - 1);
}

struct rt_gzip_decomp_args {
    uint64_t src_phys;
    uint32_t src_len;
    uint64_t dst_phys;
    uint32_t dst_len;
    int32_t  timeout_ms;
};

struct drv_gzip_decomp_inst {
    int      fd;
    uint64_t src_phy;
    void    *src_virt;
    uint32_t src_alloc_size;
    uint64_t dst_phy;
    void    *dst_virt;
    uint32_t dst_alloc_size;
};

static int _drv_gzip_decomp_resize_buffer(uint64_t *phy_addr, void **virt_addr,
                                           uint32_t *alloc_size, uint32_t size,
                                           const char *mmz_name)
{
    uint64_t new_phy;
    void    *new_virt;
    int      ret;

    if (phy_addr == NULL || virt_addr == NULL || alloc_size == NULL || mmz_name == NULL)
        return -EINVAL;

    if (size == 0)
        return -EINVAL;

    if (*virt_addr != NULL && *alloc_size == size)
        return 0;

    ret = kd_mpi_sys_mmz_alloc_cached(&new_phy, &new_virt, (char *)mmz_name, "anonymous", size);
    if (ret != 0) {
        printf("[hal_gzip_decomp]: mmz alloc %s (%u bytes) failed ret=%d\n", mmz_name, size, ret);
        return ret;
    }

    if (*virt_addr != NULL)
        kd_mpi_sys_mmz_free(*phy_addr, *virt_addr);

    *phy_addr   = new_phy;
    *virt_addr  = new_virt;
    *alloc_size = size;
    return 0;
}

/*
 * K230 UGZIP hardware stream requirements (see U-Boot lib/k230_gunzip.c and
 * TRM chapter 15 "Decompress Engine"):
 *  - The engine parses a standard gzip stream, so the method byte (CM) must
 *    be 0x08. The 0x09 method byte found in K230 firmware images is only a
 *    software marker meaning "produced by k230_priv_gzip for hardware
 *    decompression"; U-Boot restores it to 0x08 before starting the engine
 *    and this HAL does the same.
 *  - The engine expects the fixed 10-byte header. Optional fields
 *    (FEXTRA/FNAME/FCOMMENT/FHCRC) are stripped in software below.
 *  - Only dynamic-Huffman deflate blocks are supported. Streams produced by
 *    the stock gzip tool may contain stored or static-Huffman blocks and are
 *    NOT guaranteed to decompress; compress with
 *    tools/image_tools/k230_priv_gzip.py instead.
 */
static int _drv_gzip_decomp_validate_header(const void *src, uint32_t src_len)
{
    const uint8_t *header = (const uint8_t *)src;

    /* 10-byte header + 8-byte trailer (CRC32 + ISIZE) */
    if (!src || src_len < 18) {
        printf("[hal_gzip_decomp]: input too short for gzip stream (need >= 18 bytes)\n");
        return -EINVAL;
    }

    if (header[0] != 0x1f || header[1] != 0x8b) {
        printf("[hal_gzip_decomp]: bad gzip magic: %02x %02x\n", header[0], header[1]);
        return -EINVAL;
    }

    if (header[2] != 0x09) {
        if (header[2] == 0x08) {
            printf("[hal_gzip_decomp]: standard gzip is not marked as hardware-compatible; "
                   "compress with K230PrivGzip\n");
        } else {
            printf("[hal_gzip_decomp]: unsupported gzip method 0x%02x\n", header[2]);
        }
        return -ENOTSUP;
    }

    if ((header[3] & 0xe0) != 0) {
        printf("[hal_gzip_decomp]: invalid gzip flags 0x%02x\n", header[3]);
        return -EINVAL;
    }

    return 0;
}

static uint32_t _drv_gzip_decomp_get_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int _drv_gzip_decomp_validate_empty_stream(const uint8_t *stream,
                                                   uint32_t stream_len)
{
    static const uint8_t empty_tail[] = {
        0x05, 0xc1, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x10, 0xff, 0xd5, 0x08, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };

    /* K230PrivGzip levels 4-9 emit this dynamic-Huffman empty stream. Empty
     * output cannot use the hardware write-completion path, so authenticate
     * its complete deflate payload and trailer in software. */
    if (stream_len != 10 + sizeof(empty_tail) ||
        memcmp(stream + 10, empty_tail, sizeof(empty_tail)) != 0) {
        printf("[hal_gzip_decomp]: invalid hardware-compatible empty stream\n");
        return -EINVAL;
    }

    return 0;
}

/*
 * Strip optional gzip header fields so the hardware sees the fixed 10-byte
 * header immediately followed by the deflate stream. Returns the new stream
 * length, or 0 if the header is malformed.
 */
static uint32_t _drv_gzip_decomp_strip_header_extras(uint8_t *buf, uint32_t len)
{
    uint8_t flg = buf[3];
    uint32_t off = 10;

    if (flg == 0)
        return len;

    if (flg & 0x04) { /* FEXTRA */
        uint32_t xlen;

        if (len - off < 2)
            return 0;
        xlen = (uint32_t)(buf[off] | (buf[off + 1] << 8));
        off += 2;
        if (xlen > len - off)
            return 0;
        off += xlen;
    }
    if (flg & 0x08) { /* FNAME */
        while (off < len && buf[off] != 0)
            off++;
        if (off == len)
            return 0;
        off++;
    }
    if (flg & 0x10) { /* FCOMMENT */
        while (off < len && buf[off] != 0)
            off++;
        if (off == len)
            return 0;
        off++;
    }
    if (flg & 0x02) { /* FHCRC */
        if (len - off < 2)
            return 0;
        off += 2;
    }

    if (len - off < 8)
        return 0;

    buf[3] = 0;
    memmove(buf + 10, buf + off, len - off);
    return 10 + (len - off);
}

int drv_gzip_decomp_open(drv_gzip_decomp_inst_t **inst)
{
    drv_gzip_decomp_inst_t *handle;
    int ret;

    if (inst == NULL)
        return -EINVAL;

    handle = calloc(1, sizeof(*handle));
    if (handle == NULL) {
        printf("[hal_gzip_decomp]: alloc instance failed\n");
        return -ENOMEM;
    }

    handle->fd = open(DRV_GZIP_DECOMP_DEVICE_PATH, O_RDWR);
    if (handle->fd < 0) {
        printf("[hal_gzip_decomp]: open device failed errno=%d\n", errno);
        free(handle);
        return -errno;
    }

    handle->src_alloc_size = DRV_GZIP_DECOMP_DEFAULT_SRC_SIZE;
    handle->dst_alloc_size = DRV_GZIP_DECOMP_DEFAULT_DST_SIZE;

    ret = _drv_gzip_decomp_resize_buffer(&handle->src_phy, &handle->src_virt,
                                          &handle->src_alloc_size,
                                          handle->src_alloc_size, "gzip_src");
    if (ret != 0) {
        close(handle->fd);
        free(handle);
        return ret;
    }

    ret = _drv_gzip_decomp_resize_buffer(&handle->dst_phy, &handle->dst_virt,
                                          &handle->dst_alloc_size,
                                          handle->dst_alloc_size, "gzip_dst");
    if (ret != 0) {
        kd_mpi_sys_mmz_free(handle->src_phy, handle->src_virt);
        close(handle->fd);
        free(handle);
        return ret;
    }

    *inst = handle;
    return 0;
}

void drv_gzip_decomp_close(drv_gzip_decomp_inst_t **inst)
{
    if (inst == NULL || *inst == NULL)
        return;

    if ((*inst)->dst_virt)
        kd_mpi_sys_mmz_free((*inst)->dst_phy, (*inst)->dst_virt);
    if ((*inst)->src_virt)
        kd_mpi_sys_mmz_free((*inst)->src_phy, (*inst)->src_virt);

    if ((*inst)->fd >= 0)
        close((*inst)->fd);

    free(*inst);
    *inst = NULL;
}

int drv_gzip_decomp_set_src_alloc_size(drv_gzip_decomp_inst_t *inst, uint32_t size)
{
    if (inst == NULL || inst->fd < 0)
        return -EINVAL;

    return _drv_gzip_decomp_resize_buffer(&inst->src_phy, &inst->src_virt,
                                           &inst->src_alloc_size, size, "gzip_src");
}

int drv_gzip_decomp_set_dst_alloc_size(drv_gzip_decomp_inst_t *inst, uint32_t size)
{
    if (inst == NULL || inst->fd < 0)
        return -EINVAL;

    return _drv_gzip_decomp_resize_buffer(&inst->dst_phy, &inst->dst_virt,
                                           &inst->dst_alloc_size, size, "gzip_dst");
}

uint32_t drv_gzip_decomp_get_src_alloc_size(const drv_gzip_decomp_inst_t *inst)
{
    if (inst == NULL)
        return 0;
    return inst->src_alloc_size;
}

uint32_t drv_gzip_decomp_get_dst_alloc_size(const drv_gzip_decomp_inst_t *inst)
{
    if (inst == NULL)
        return 0;
    return inst->dst_alloc_size;
}

int drv_gzip_decomp_gunzip_phys(drv_gzip_decomp_inst_t *inst,
                                uint64_t src_phys, uint32_t src_len,
                                uint64_t dst_phys, uint32_t dst_len,
                                int32_t timeout_ms)
{
    struct rt_gzip_decomp_args args;
    int ret;

    if (inst == NULL || inst->fd < 0) {
        printf("[hal_gzip_decomp]: invalid instance\n");
        return -EINVAL;
    }

    args.src_phys   = src_phys;
    args.src_len    = src_len;
    args.dst_phys   = dst_phys;
    args.dst_len    = dst_len;
    args.timeout_ms = timeout_ms;

    ret = ioctl(inst->fd, RT_GZIP_DECOMP_GUNZIP, &args);
    if (ret != 0) {
        printf("[hal_gzip_decomp]: gunzip ioctl failed: %d\n", ret);
        return ret;
    }

    return 0;
}

int drv_gzip_decomp_gunzip(drv_gzip_decomp_inst_t *inst,
                           const void *src, uint32_t src_len,
                           void *dst, uint32_t dst_len,
                           int32_t timeout_ms)
{
    uint8_t *hdr;
    uint32_t src_dma_size;
    uint32_t dst_dma_size;
    int ret;

    if (inst == NULL || inst->fd < 0) {
        printf("[hal_gzip_decomp]: invalid instance\n");
        return -EINVAL;
    }

    ret = _drv_gzip_decomp_validate_header(src, src_len);
    if (ret != 0)
        return ret;

    if (_drv_gzip_decomp_get_le32((const uint8_t *)src + src_len - 4) != dst_len) {
        printf("[hal_gzip_decomp]: gzip output size does not match destination (%u)\n", dst_len);
        return -EINVAL;
    }
    if (dst_len != 0 && dst == NULL) {
        printf("[hal_gzip_decomp]: destination is null for non-empty output\n");
        return -EINVAL;
    }

    src_dma_size = _drv_gzip_decomp_align_node(src_len);
    if (src_dma_size < src_len)
        return -EOVERFLOW;

    /* Pad allocations to the 128KB DMA node size: the engine's SDMA chains
     * always transfer whole nodes, so the last node may read/write up to
     * 128KB past the payload. */
    if (src_dma_size > inst->src_alloc_size) {
        ret = drv_gzip_decomp_set_src_alloc_size(inst, src_dma_size);
        if (ret != 0)
            return ret;
    }

    memset(inst->src_virt, 0, src_dma_size);
    hal_rvv_memcpy(inst->src_virt, src, src_len);
    hdr = (uint8_t *)inst->src_virt;

    /* The 0x09 byte is a software marker. The hardware expects DEFLATE (0x08). */
    hdr[2] = 0x08;

    /* Hardware expects the fixed 10-byte header: drop FNAME etc. */
    src_len = _drv_gzip_decomp_strip_header_extras(hdr, src_len);
    if (src_len == 0) {
        printf("[hal_gzip_decomp]: malformed gzip header (bad optional fields)\n");
        return -EINVAL;
    }

    memset(hdr + src_len, 0, src_dma_size - src_len);

    if (dst_len == 0)
        return _drv_gzip_decomp_validate_empty_stream(hdr, src_len);

    dst_dma_size = _drv_gzip_decomp_align_node(dst_len);
    if (dst_dma_size < dst_len)
        return -EOVERFLOW;
    if (dst_dma_size > inst->dst_alloc_size) {
        ret = drv_gzip_decomp_set_dst_alloc_size(inst, dst_dma_size);
        if (ret != 0)
            return ret;
    }

    kd_mpi_sys_mmz_flush_cache(inst->src_phy, inst->src_virt, src_dma_size);

    ret = drv_gzip_decomp_gunzip_phys(inst, inst->src_phy, src_len,
                                      inst->dst_phy, dst_len, timeout_ms);

    if (ret == 0) {
        kd_mpi_sys_mmz_invalidate_cache(inst->dst_phy, inst->dst_virt, dst_len);
        hal_rvv_memcpy(dst, inst->dst_virt, dst_len);
    }

    return ret;
}
