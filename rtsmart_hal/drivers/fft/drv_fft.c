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

#include "drv_fft.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "hal_rvv_ops.h"
#include "mpi_sys_api.h"

/* FFT device ioctl ABI — must match kernel driver */
#ifndef K_IOC_TYPE_FFT
#define K_IOC_TYPE_FFT 'f'
#endif

#define FFT_HAL_LOG(...) // Fprintf("[fft-hal] " __VA_ARGS__)

typedef struct {
    uint32_t point;
    uint32_t mode;
    uint32_t input_mode;
    uint32_t output_mode;
    uint16_t shift;
    uint16_t reserved0;
    uint32_t timeout_ms;
    uint64_t input_phy_addr;
    uint32_t input_len;
    uint32_t reserved1;
    uint64_t output_phy_addr;
    uint32_t output_len;
    uint32_t reserved2;
} k_fft_run_request;

#define KD_IOC_CMD_FFT_RUN _IOW(K_IOC_TYPE_FFT, 204, k_fft_run_request)

#define DRV_FFT_DEVICE_PATH "/dev/fft"

/* Maximum DMA buffer size for largest supported FFT (4096 points, RR_II mode). */
#define DRV_FFT_MAX_DMA_BYTES (4096 * sizeof(int16_t) * 2)

struct drv_fft_inst {
    int      fd;
    uint64_t input_phy;
    void    *input_virt;
    uint32_t input_alloc_size;
    uint64_t output_phy;
    void    *output_virt;
    uint32_t output_alloc_size;
};

static int drv_fft_point_valid(uint32_t point)
{
    switch (point) {
    case 64:
    case 128:
    case 256:
    case 512:
    case 1024:
    case 2048:
    case 4096:
        return 1;
    default:
        return 0;
    }
}

static uint32_t drv_fft_input_bytes(const drv_fft_cfg_t* cfg)
{
    uint32_t bytes = cfg->point * sizeof(short) * 2;

    if (cfg->input_mode == RRRR) {
        bytes /= 2;
    }

    return bytes;
}

static uint32_t drv_fft_output_bytes(const drv_fft_cfg_t* cfg) { return cfg->point * sizeof(short) * 2; }

static int drv_fft_resize_buffer(uint64_t* phy_addr, void** virt_addr, uint32_t* alloc_size, uint32_t size,
                                 const char* mmz_name)
{
    uint64_t new_phy;
    void*    new_virt;
    int      ret;

    if (phy_addr == NULL || virt_addr == NULL || alloc_size == NULL || mmz_name == NULL) {
        return -EINVAL;
    }

    if (size == 0) {
        return -EINVAL;
    }

    if (*virt_addr != NULL && *alloc_size == size) {
        return 0;
    }

    ret = kd_mpi_sys_mmz_alloc_cached(&new_phy, &new_virt, (char*)mmz_name, "anonymous", size);
    if (ret != 0) {
        printf("fft alloc %s failed ret=%d\n", mmz_name, ret);
        return ret;
    }

    if (*virt_addr != NULL) {
        kd_mpi_sys_mmz_free(*phy_addr, *virt_addr);
    }

    *phy_addr = new_phy;
    *virt_addr = new_virt;
    *alloc_size = size;

    return 0;
}

static int drv_fft_validate(const drv_fft_cfg_t* cfg, const short* in_real, const short* in_imag, short* out_real,
                            short* out_imag)
{
    if (cfg == NULL || in_real == NULL || out_real == NULL || out_imag == NULL) {
        return -EINVAL;
    }

    if (!drv_fft_point_valid(cfg->point)) {
        return -EINVAL;
    }

    if (cfg->mode > IFFT_MODE || cfg->input_mode > RR_II || cfg->output_mode > RR_II_OUT) {
        return -EINVAL;
    }

    if (cfg->input_mode != RRRR && in_imag == NULL) {
        return -EINVAL;
    }

    return 0;
}

static void drv_fft_pack_input(const drv_fft_cfg_t* cfg, const short* in_real, const short* in_imag, uint64_t* dst)
{
    uint32_t index;
    uint64_t value;

    if (cfg->input_mode == RIRI) {
        for (index = 0; index < cfg->point; index += 2) {
            value = ((uint64_t)(uint16_t)in_imag[index + 1] << 48) | ((uint64_t)(uint16_t)in_real[index + 1] << 32)
                | ((uint64_t)(uint16_t)in_imag[index] << 16) | (uint64_t)(uint16_t)in_real[index];
            dst[index / 2] = value;
        }
        return;
    }

    if (cfg->input_mode == RRRR) {
        hal_rvv_memcpy(dst, in_real, cfg->point * sizeof(short));
        return;
    }

    if (cfg->input_mode == RR_II) {
        hal_rvv_memcpy(dst, in_real, cfg->point * sizeof(short));
        hal_rvv_memcpy(((unsigned char*)dst) + (cfg->point * sizeof(short)), in_imag, cfg->point * sizeof(short));
        return;
    }

    for (index = 0; index < cfg->point; index += 4) {
        value = ((uint64_t)(uint16_t)in_real[index + 3] << 48) | ((uint64_t)(uint16_t)in_real[index + 2] << 32)
            | ((uint64_t)(uint16_t)in_real[index + 1] << 16) | (uint64_t)(uint16_t)in_real[index];
        dst[index / 4] = value;
    }

    for (index = 0; index < cfg->point; index += 4) {
        value = ((uint64_t)(uint16_t)in_imag[index + 3] << 48) | ((uint64_t)(uint16_t)in_imag[index + 2] << 32)
            | ((uint64_t)(uint16_t)in_imag[index + 1] << 16) | (uint64_t)(uint16_t)in_imag[index];
        dst[(cfg->point / 4) + (index / 4)] = value;
    }
}

static void drv_fft_unpack_output(const drv_fft_cfg_t* cfg, const uint64_t* src, short* out_real, short* out_imag)
{
    uint32_t index;
    uint64_t value;

    if (cfg->output_mode == RIRI_OUT) {
        for (index = 0; index < cfg->point / 2; ++index) {
            value                   = src[index];
            out_real[index * 2]     = (short)(value & 0xffff);
            out_imag[index * 2]     = (short)((value >> 16) & 0xffff);
            out_real[index * 2 + 1] = (short)((value >> 32) & 0xffff);
            out_imag[index * 2 + 1] = (short)((value >> 48) & 0xffff);
        }
        return;
    }

    hal_rvv_memcpy(out_real, src, cfg->point * sizeof(short));
    hal_rvv_memcpy(out_imag, ((const unsigned char*)src) + (cfg->point * sizeof(short)), cfg->point * sizeof(short));
    return;
}

int drv_fft_open(drv_fft_inst_t** inst)
{
    drv_fft_inst_t* handle;
    int             ret;

    if (inst == NULL) {
        return -EINVAL;
    }

    handle = calloc(1, sizeof(*handle));
    if (handle == NULL) {
        printf("open alloc failed\n");
        return -ENOMEM;
    }

    FFT_HAL_LOG("opening %s\n", DRV_FFT_DEVICE_PATH);
    handle->fd = open(DRV_FFT_DEVICE_PATH, O_RDWR);
    if (handle->fd < 0) {
        printf("open failed errno=%d\n", errno);
        free(handle);
        return -errno;
    }

    /* Pre-allocate DMA buffers so we avoid repeated MMZ alloc/free per frame. */
    handle->input_alloc_size  = DRV_FFT_MAX_DMA_BYTES;
    handle->output_alloc_size = DRV_FFT_MAX_DMA_BYTES;

    ret = drv_fft_resize_buffer(&handle->input_phy, &handle->input_virt,
                                &handle->input_alloc_size, handle->input_alloc_size, "fft_in");
    if (ret != 0) {
        printf("fft pre-alloc input failed ret=%d\n", ret);
        close(handle->fd);
        free(handle);
        return ret;
    }

    ret = drv_fft_resize_buffer(&handle->output_phy, &handle->output_virt,
                                &handle->output_alloc_size, handle->output_alloc_size, "fft_out");
    if (ret != 0) {
        printf("fft pre-alloc output failed ret=%d\n", ret);
        kd_mpi_sys_mmz_free(handle->input_phy, handle->input_virt);
        close(handle->fd);
        free(handle);
        return ret;
    }

    FFT_HAL_LOG("open ok fd=%d in=0x%lx out=0x%lx\n", handle->fd,
                (unsigned long)handle->input_phy, (unsigned long)handle->output_phy);

    *inst = handle;
    return 0;
}

void drv_fft_close(drv_fft_inst_t** inst)
{
    if (inst == NULL || *inst == NULL) {
        return;
    }

    if ((*inst)->output_virt) {
        kd_mpi_sys_mmz_free((*inst)->output_phy, (*inst)->output_virt);
    }
    if ((*inst)->input_virt) {
        kd_mpi_sys_mmz_free((*inst)->input_phy, (*inst)->input_virt);
    }

    if ((*inst)->fd >= 0) {
        close((*inst)->fd);
    }

    free(*inst);
    *inst = NULL;
}

int drv_fft_set_input_alloc_size(drv_fft_inst_t* inst, uint32_t size)
{
    if (inst == NULL || inst->fd < 0) {
        return -EINVAL;
    }

    return drv_fft_resize_buffer(&inst->input_phy, &inst->input_virt,
                                 &inst->input_alloc_size, size, "fft_in");
}

int drv_fft_set_output_alloc_size(drv_fft_inst_t* inst, uint32_t size)
{
    if (inst == NULL || inst->fd < 0) {
        return -EINVAL;
    }

    return drv_fft_resize_buffer(&inst->output_phy, &inst->output_virt,
                                 &inst->output_alloc_size, size, "fft_out");
}

uint32_t drv_fft_get_input_alloc_size(const drv_fft_inst_t* inst)
{
    if (inst == NULL) {
        return 0;
    }

    return inst->input_alloc_size;
}

uint32_t drv_fft_get_output_alloc_size(const drv_fft_inst_t* inst)
{
    if (inst == NULL) {
        return 0;
    }

    return inst->output_alloc_size;
}

int drv_fft_run(drv_fft_inst_t* inst, const drv_fft_cfg_t* cfg, const short* in_real, const short* in_imag, short* out_real,
                short* out_imag)
{
    k_fft_run_request req;
    int               ret;

    if (inst == NULL || inst->fd < 0) {
        printf("run rejected invalid instance\n");
        return -EINVAL;
    }

    ret = drv_fft_validate(cfg, in_real, in_imag, out_real, out_imag);
    if (ret != 0) {
        printf("validate failed ret=%d\n", ret);
        return ret;
    }

    FFT_HAL_LOG("run point=%u mode=%u im=%u om=%u shift=0x%x timeout=%u fd=%d\n", cfg->point, cfg->mode, cfg->input_mode,
                cfg->output_mode, cfg->shift, cfg->timeout_ms, inst->fd);

    memset(&req, 0, sizeof(req));
    req.point       = cfg->point;
    req.mode        = cfg->mode;
    req.input_mode  = cfg->input_mode;
    req.output_mode = cfg->output_mode;
    req.shift       = cfg->shift;
    req.timeout_ms  = cfg->timeout_ms;
    req.input_len   = drv_fft_input_bytes(cfg);
    req.output_len  = drv_fft_output_bytes(cfg);

    /* Use pre-allocated DMA buffers; verify they are large enough. */
    if (req.input_len > inst->input_alloc_size || req.output_len > inst->output_alloc_size) {
        printf("fft buffer too small: need in=%u/%u out=%u/%u\n",
               req.input_len, inst->input_alloc_size,
               req.output_len, inst->output_alloc_size);
        return -ENOMEM;
    }

    FFT_HAL_LOG("mmz in=0x%lx/%u out=0x%lx/%u\n", (unsigned long)inst->input_phy, req.input_len,
                (unsigned long)inst->output_phy, req.output_len);

    drv_fft_pack_input(cfg, in_real, in_imag, (uint64_t*)inst->input_virt);
    memset(inst->output_virt, 0, req.output_len);

    ret = kd_mpi_sys_mmz_flush_cache(inst->input_phy, inst->input_virt, req.input_len);
    if (ret != 0) {
        printf("flush input failed ret=%d", ret);
        return ret;
    }

    ret = kd_mpi_sys_mmz_flush_cache(inst->output_phy, inst->output_virt, req.output_len);
    if (ret != 0) {
        printf("flush output failed ret=%d\n", ret);
        return ret;
    }

    req.input_phy_addr  = inst->input_phy;
    req.output_phy_addr = inst->output_phy;

    FFT_HAL_LOG("ioctl cmd=0x%lx\n", (unsigned long)KD_IOC_CMD_FFT_RUN);
    ret = ioctl(inst->fd, KD_IOC_CMD_FFT_RUN, &req);
    if (ret < 0) {
        printf("ioctl failed ret=%d errno=%d\n", ret, errno);
        return (errno != 0) ? -errno : ret;
    }

    FFT_HAL_LOG("ioctl ok ret=%d\n", ret);

    ret = kd_mpi_sys_mmz_invalidate_cache(inst->output_phy, inst->output_virt, req.output_len);
    if (ret != 0) {
        printf("invalidate output failed ret=%d\n", ret);
        return ret;
    }

    drv_fft_unpack_output(cfg, (const uint64_t*)inst->output_virt, out_real, out_imag);

    return ret;
}

int drv_fft_fft(drv_fft_inst_t* inst, const drv_fft_cfg_t* cfg, const short* in_real, const short* in_imag, short* out_real,
                short* out_imag)
{
    drv_fft_cfg_t local_cfg;

    if (cfg == NULL) {
        return -EINVAL;
    }

    local_cfg      = *cfg;
    local_cfg.mode = FFT_MODE;
    return drv_fft_run(inst, &local_cfg, in_real, in_imag, out_real, out_imag);
}

int drv_fft_ifft(drv_fft_inst_t* inst, const drv_fft_cfg_t* cfg, const short* in_real, const short* in_imag, short* out_real,
                 short* out_imag)
{
    drv_fft_cfg_t local_cfg;

    if (cfg == NULL) {
        return -EINVAL;
    }

    local_cfg      = *cfg;
    local_cfg.mode = IFFT_MODE;
    return drv_fft_run(inst, &local_cfg, in_real, in_imag, out_real, out_imag);
}