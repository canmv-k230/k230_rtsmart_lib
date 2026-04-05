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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FFT_MAX_POINT 4096

typedef enum {
    FFT_MODE = 0,
    IFFT_MODE,
} k_fft_mode_e;

typedef enum {
    RIRI = 0,
    RRRR,
    RR_II,
} k_fft_input_mode_e;

typedef enum {
    RIRI_OUT = 0,
    RR_II_OUT,
} k_fft_out_mode_e;

typedef struct drv_fft_inst drv_fft_inst_t;

typedef struct {
    uint32_t           point;
    k_fft_mode_e       mode;
    k_fft_input_mode_e input_mode;
    k_fft_out_mode_e   output_mode;
    uint16_t           shift;
    uint32_t           timeout_ms;
} drv_fft_cfg_t;

int  drv_fft_open(drv_fft_inst_t** inst);
void drv_fft_close(drv_fft_inst_t** inst);

int  drv_fft_set_input_alloc_size(drv_fft_inst_t* inst, uint32_t size);
int  drv_fft_set_output_alloc_size(drv_fft_inst_t* inst, uint32_t size);
uint32_t drv_fft_get_input_alloc_size(const drv_fft_inst_t* inst);
uint32_t drv_fft_get_output_alloc_size(const drv_fft_inst_t* inst);

int  drv_fft_run(drv_fft_inst_t* inst, const drv_fft_cfg_t* cfg, const short* in_real, const short* in_imag, short* out_real,
                 short* out_imag);
int  drv_fft_fft(drv_fft_inst_t* inst, const drv_fft_cfg_t* cfg, const short* in_real, const short* in_imag, short* out_real,
                 short* out_imag);
int  drv_fft_ifft(drv_fft_inst_t* inst, const drv_fft_cfg_t* cfg, const short* in_real, const short* in_imag, short* out_real,
                  short* out_imag);

#ifdef __cplusplus
}
#endif
