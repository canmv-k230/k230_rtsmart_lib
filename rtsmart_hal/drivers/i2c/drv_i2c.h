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

#include <stdint.h>

#define KD_HARD_I2C_MAX_NUM (5)

#define DRV_I2C_TYPE_HARD  (0x01)
#define DRV_I2C_TYPE_SOFT  (0x02)
#define DRV_I2C_TYPE_SLAVE (0x03)

/* flag for master */
#define DRV_I2C_WR          0x0000
#define DRV_I2C_RD          (1u << 0)
#define DRV_I2C_ADDR_10BIT  (1u << 2) /* this is a ten bit chip address */
#define DRV_I2C_NO_START    (1u << 4)
#define DRV_I2C_IGNORE_NACK (1u << 5)
#define DRV_I2C_NO_READ_ACK (1u << 6) /* when I2C reading, we do not ACK */
#define DRV_I2C_NO_STOP     (1u << 7)
#define RT_I2C_STOP         (0x8000)

// typedef struct _drv_i2c_pin_cfg {
//     uint8_t pin_scl;
//     uint8_t pin_sda;
// } drv_i2c_pin_cfg_t;

// int drv_i2c_get_default_pins(int id, drv_i2c_pin_cfg_t *pins);
// int drv_i2c_install_pins(int id, drv_i2c_pin_cfg_t* pins);

/** definitions for i2c master ***********************************************/

typedef struct _drv_i2c_inst {
    void* base;

    int type, id, fd;

    uint8_t pin_scl;
    uint8_t pin_sda;

    uint32_t freq;
    uint32_t timeout_ms;
} drv_i2c_inst_t;

typedef struct _i2c_msg {
    uint16_t addr;
    uint16_t flags;
    uint16_t len;
    uint8_t* buf;
} i2c_msg_t;

int drv_i2c_inst_create(int id, uint32_t freq, uint32_t timeout_ms, uint8_t scl, uint8_t sda, drv_i2c_inst_t** inst);
void drv_i2c_inst_destroy(drv_i2c_inst_t** inst);

int drv_i2c_set_7b_addr(drv_i2c_inst_t* inst);
int drv_i2c_set_10b_addr(drv_i2c_inst_t* inst);
int drv_i2c_set_freq(drv_i2c_inst_t* inst, uint32_t freq);
int drv_i2c_set_timeout(drv_i2c_inst_t* inst, uint32_t timeout_ms);

int drv_i2c_transfer(drv_i2c_inst_t* inst, i2c_msg_t* msgs, int msg_cnt);

#ifndef MEMBER_TYPE
#  ifdef __cplusplus
     // C++ equivalent
#    define MEMBER_TYPE(struct_type, member) decltype(((struct_type*)0)->member)
#  else
     // C extension
#    define MEMBER_TYPE(struct_type, member) typeof(((struct_type*)0)->member)
#  endif
#endif

#define DRV_I2C_GET_ATTR_TEMPLATE(struct_type, member, type)                                                           \
    static inline __attribute__((always_inline)) MEMBER_TYPE(struct_type, member)                                      \
        drv_i2c_##type##_get_##member(struct_type* inst)                                                               \
    {                                                                                                                  \
        if (!inst) {                                                                                                   \
            return -1;                                                                                                 \
        }                                                                                                              \
        return inst->member;                                                                                           \
    }

// uint32_t drv_i2c_master_get_type(drv_i2c_inst_t *inst);
DRV_I2C_GET_ATTR_TEMPLATE(drv_i2c_inst_t, type, master)

// int drv_i2c_master_get_id(drv_i2c_inst_t *inst);
DRV_I2C_GET_ATTR_TEMPLATE(drv_i2c_inst_t, id, master)

// int drv_i2c_master_get_fd(drv_i2c_inst_t *inst);
DRV_I2C_GET_ATTR_TEMPLATE(drv_i2c_inst_t, fd, master)

// uint32_t drv_i2c_master_get_freq(drv_i2c_inst_t *inst);
DRV_I2C_GET_ATTR_TEMPLATE(drv_i2c_inst_t, freq, master)

// uint32_t drv_i2c_master_get_timeout_ms(drv_i2c_inst_t *inst);
DRV_I2C_GET_ATTR_TEMPLATE(drv_i2c_inst_t, timeout_ms, master)

// uint8_t drv_i2c_master_get_pin_scl(drv_i2c_inst_t *inst);
DRV_I2C_GET_ATTR_TEMPLATE(drv_i2c_inst_t, pin_scl, master)

// uint8_t drv_i2c_master_get_pin_sda(drv_i2c_inst_t *inst);
DRV_I2C_GET_ATTR_TEMPLATE(drv_i2c_inst_t, pin_sda, master)

/** definitions for i2c slave ************************************************/

typedef struct _drv_i2c_slave_inst {
    void* base;

    int id, fd;

    uint8_t pin_scl;
    uint8_t pin_sda;

    uint32_t buffer_size;
    uint16_t slave_address;
} drv_i2c_slave_inst_t;

int drv_i2c_slave_inst_create(int id, uint32_t buffer_size, uint16_t slave_address, uint8_t scl, uint8_t sda,
                              drv_i2c_slave_inst_t** inst);

// int drv_i2c_slave_get_id(drv_i2c_slave_inst_t *inst);
DRV_I2C_GET_ATTR_TEMPLATE(drv_i2c_slave_inst_t, id, slave)

// int drv_i2c_slave_get_fd(drv_i2c_slave_inst_t *inst);
DRV_I2C_GET_ATTR_TEMPLATE(drv_i2c_slave_inst_t, fd, slave)

// uint8_t drv_i2c_slave_get_pin_scl(drv_i2c_inst_t *inst);
DRV_I2C_GET_ATTR_TEMPLATE(drv_i2c_inst_t, pin_scl, slave)

// uint8_t drv_i2c_slave_get_pin_sda(drv_i2c_inst_t *inst);
DRV_I2C_GET_ATTR_TEMPLATE(drv_i2c_inst_t, pin_sda, slave)

// uint32_t drv_i2c_slave_get_buffer_size(drv_i2c_slave_inst_t *inst);
DRV_I2C_GET_ATTR_TEMPLATE(drv_i2c_slave_inst_t, buffer_size, slave)

// uint16_t drv_i2c_slave_get_slave_address(drv_i2c_slave_inst_t *inst);
DRV_I2C_GET_ATTR_TEMPLATE(drv_i2c_slave_inst_t, slave_address, slave)

#undef DRV_I2C_GET_ATTR_TEMPLATE
#undef MEMBER_TYPE

#ifdef __cplusplus
}
#endif
