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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct drv_gzip_decomp_inst drv_gzip_decomp_inst_t;

int  drv_gzip_decomp_open(drv_gzip_decomp_inst_t **inst);
void drv_gzip_decomp_close(drv_gzip_decomp_inst_t **inst);

int      drv_gzip_decomp_set_src_alloc_size(drv_gzip_decomp_inst_t *inst, uint32_t size);
int      drv_gzip_decomp_set_dst_alloc_size(drv_gzip_decomp_inst_t *inst, uint32_t size);
uint32_t drv_gzip_decomp_get_src_alloc_size(const drv_gzip_decomp_inst_t *inst);
uint32_t drv_gzip_decomp_get_dst_alloc_size(const drv_gzip_decomp_inst_t *inst);

int drv_gzip_decomp_gunzip(drv_gzip_decomp_inst_t *inst,
                           const void *src, uint32_t src_len,
                           void *dst, uint32_t dst_len,
                           int32_t timeout_ms);

int drv_gzip_decomp_gunzip_phys(drv_gzip_decomp_inst_t *inst,
                                uint64_t src_phys, uint32_t src_len,
                                uint64_t dst_phys, uint32_t dst_len,
                                int32_t timeout_ms);

#ifdef __cplusplus
}
#endif
