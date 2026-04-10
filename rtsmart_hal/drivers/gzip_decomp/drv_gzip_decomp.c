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

static int _drv_gzip_decomp_validate_header(const void *src, uint32_t src_len)
{
    const uint8_t *header = (const uint8_t *)src;

    if (!src || src_len < 10) {
        printf("[hal_gzip_decomp]: input too short for gzip header (need >= 10 bytes)\n");
        return -1;
    }

    if (header[0] != 0x1f || header[1] != 0x8b) {
        printf("[hal_gzip_decomp]: bad gzip magic: %02x %02x\n", header[0], header[1]);
        return -1;
    }

    if (header[2] != 0x08 && header[2] != 0x09) {
        printf("[hal_gzip_decomp]: unsupported gzip method 0x%02x (expect 0x08 or 0x09)\n", header[2]);
        return -1;
    }

    return 0;
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
    int ret;

    if (inst == NULL || inst->fd < 0) {
        printf("[hal_gzip_decomp]: invalid instance\n");
        return -EINVAL;
    }

    ret = _drv_gzip_decomp_validate_header(src, src_len);
    if (ret != 0)
        return ret;

    if (src_len > inst->src_alloc_size) {
        ret = drv_gzip_decomp_set_src_alloc_size(inst, src_len);
        if (ret != 0)
            return ret;
    }

    if (dst_len > inst->dst_alloc_size) {
        ret = drv_gzip_decomp_set_dst_alloc_size(inst, dst_len);
        if (ret != 0)
            return ret;
    }

    hal_rvv_memcpy(inst->src_virt, src, src_len);

    /* Auto-patch standard gzip (method=0x08) to K230 private format (0x09) */
    if (((uint8_t *)inst->src_virt)[2] == 0x08)
        ((uint8_t *)inst->src_virt)[2] = 0x09;

    kd_mpi_sys_mmz_flush_cache(inst->src_phy, inst->src_virt, src_len);

    ret = drv_gzip_decomp_gunzip_phys(inst, inst->src_phy, src_len,
                                      inst->dst_phy, dst_len, timeout_ms);

    if (ret == 0) {
        kd_mpi_sys_mmz_invalidate_cache(inst->dst_phy, inst->dst_virt, dst_len);
        hal_rvv_memcpy(dst, inst->dst_virt, dst_len);
    }

    return ret;
}
