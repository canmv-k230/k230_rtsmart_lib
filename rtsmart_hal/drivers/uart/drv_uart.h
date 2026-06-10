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
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <sys/time.h>
#include <sys/types.h>

#define KD_HARD_UART_MAX_NUM (5)

#define DATA_BITS_5 5
#define DATA_BITS_6 6
#define DATA_BITS_7 7
#define DATA_BITS_8 8
#define DATA_BITS_9 9

#define STOP_BITS_1 0
#define STOP_BITS_2 1
#define STOP_BITS_3 2
#define STOP_BITS_4 3

#define PARITY_NONE 0
#define PARITY_ODD  1
#define PARITY_EVEN 2

#define BIT_ORDER_LSB 0
#define BIT_ORDER_MSB 1

#define NRZ_NORMAL   0 /* Non Return to Zero : normal mode */
#define NRZ_INVERTED 1 /* Non Return to Zero : inverted mode */

struct uart_configure {
    uint32_t baud_rate;

    uint32_t data_bits : 4;
    uint32_t stop_bits : 2;
    uint32_t parity : 2;
    uint32_t bit_order : 1;
    uint32_t invert : 1;
    uint32_t bufsz : 16;
    uint32_t reserved : 6;
};

typedef struct _drv_uart_inst {
    void* base;

    int id, fd;

    struct uart_configure curr_config;
} drv_uart_inst_t;

#define drv_uart_inst_create(id, inst)      _drv_uart_inst_create(id, NULL, inst)
#define drv_uart_inst_create_usb(dev, inst) _drv_uart_inst_create(-1, dev, inst)

int  _drv_uart_inst_create(int id, const char* dev, drv_uart_inst_t** inst);
void drv_uart_inst_destroy(drv_uart_inst_t** inst);

ssize_t drv_uart_read(drv_uart_inst_t* inst, uint8_t* buffer, size_t size);
ssize_t drv_uart_write(drv_uart_inst_t* inst, const uint8_t* buffer, size_t size);

int drv_uart_poll(drv_uart_inst_t* inst, int timeout_ms);

ssize_t drv_uart_recv_available(drv_uart_inst_t* inst);

int drv_uart_configure_buffer_size(int id, uint16_t size);

int drv_uart_is_dtr_asserted(drv_uart_inst_t* inst);

int drv_uart_send_break(drv_uart_inst_t* inst);

int drv_uart_set_config(drv_uart_inst_t* inst, struct uart_configure* cfg);
int drv_uart_get_config(drv_uart_inst_t* inst, struct uart_configure* cfg);

#ifndef MEMBER_TYPE
#  ifdef __cplusplus
     // C++ equivalent
#    define MEMBER_TYPE(struct_type, member) decltype(((struct_type*)0)->member)
#  else
     // C extension
#    define MEMBER_TYPE(struct_type, member) typeof(((struct_type*)0)->member)
#  endif
#endif

#define DRV_UART_GET_ATTR_TEMPLATE(struct_type, member)                                                                        \
    static inline __attribute__((always_inline)) MEMBER_TYPE(struct_type, member) drv_uart_##get_##member(struct_type* inst)   \
    {                                                                                                                          \
        if (!inst) {                                                                                                           \
            return -1;                                                                                                         \
        }                                                                                                                      \
        return inst->member;                                                                                                   \
    }

// int drv_uart_get_id(drv_uart_inst_t *inst);
DRV_UART_GET_ATTR_TEMPLATE(drv_uart_inst_t, id)

// int drv_uart_get_fd(drv_uart_inst_t *inst);
DRV_UART_GET_ATTR_TEMPLATE(drv_uart_inst_t, fd)

#ifdef __cplusplus
}
#endif
