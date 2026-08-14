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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "canmv_misc.h"
#include "drv_fpioa.h"

#include "drv_i2c.h"

#define RT_I2C_DEV_CTRL_10BIT (8 * 0x100 + 0x01)
// #define RT_I2C_DEV_CTRL_ADDR    (8 * 0x100 + 0x02)
#define RT_I2C_DEV_CTRL_TIMEOUT (8 * 0x100 + 0x03)
#define RT_I2C_DEV_CTRL_RW      (8 * 0x100 + 0x04)
#define RT_I2C_DEV_CTRL_CLK     (8 * 0x100 + 0x05)
#define RT_I2C_DEV_CTRL_7BIT    (8 * 0x100 + 0x06)

static const int i2c_master_inst_type = 0;

static int hard_i2c_in_use[KD_HARD_I2C_MAX_NUM];

static int drv_i2c_ioctl(drv_i2c_inst_t* inst, int cmd, void* arg)
{
    if ((NULL == inst) || (0x00 > inst->fd)) {
        printf("[hal_i2c]: i2c device not open\n");
        return -1;
    }

    return ioctl(inst->fd, cmd, arg);
}

int drv_i2c_inst_create(int id, uint32_t freq, uint32_t timeout_ms, uint8_t scl, uint8_t sda, drv_i2c_inst_t** inst)
{
    int  fd = -1;
    char dev_name[64];
    int  dev_type = DRV_I2C_TYPE_HARD;

    uint8_t soft_scl = scl, soft_sda = sda;

    if (NULL == inst) {
        return -1;
    }

    if (KD_HARD_I2C_MAX_NUM > id) {
        if (hard_i2c_in_use[id]) {
            printf("[hal_i2c]: i2c%d maybe in use\n", id);
        }
    }

    if (*inst) {
        drv_i2c_inst_destroy(inst);
        *inst = NULL;
    }

    if (KD_HARD_I2C_MAX_NUM > id) {
        dev_type = DRV_I2C_TYPE_HARD;
    } else {
        if ((64 <= soft_scl) || (64 <= soft_sda)) {
            printf("[hal_i2c]: invalid scl(%d), sda(%d) for soft i2c\n", soft_scl, soft_sda);
            return -1;
        }

        struct soft_i2c_configure cfg;

        cfg.bus_num    = id;
        cfg.pin_scl    = soft_scl;
        cfg.pin_sda    = soft_sda;
        cfg.freq       = freq;
        cfg.timeout_ms = timeout_ms;

        if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_CREATE_SOFT_I2C, &cfg)) {
            printf("[hal_i2c]: can't create soft i2c device");

            return -1;
        }
        dev_type = DRV_I2C_TYPE_SOFT;
    }
    snprintf(dev_name, sizeof(dev_name), "/dev/i2c%d", id);

    if (0 > (fd = open(dev_name, O_RDWR))) {
        printf("[hal_i2c]: open %s failed\n", dev_name);
        return -1;
    }

    *inst = malloc(sizeof(drv_i2c_inst_t));
    if (NULL == *inst) {
        printf("[hal_i2c]: malloc failed");

        close(fd);
        return -1;
    }
    memset(*inst, 0x00, sizeof(drv_i2c_inst_t));

    (*inst)->base       = (void*)&i2c_master_inst_type;
    (*inst)->type       = dev_type;
    (*inst)->id         = id;
    (*inst)->fd         = fd;
    (*inst)->pin_scl    = soft_scl;
    (*inst)->pin_sda    = soft_sda;
    (*inst)->freq       = freq;
    (*inst)->timeout_ms = timeout_ms;

    if (DRV_I2C_TYPE_HARD == dev_type) {
        hard_i2c_in_use[id] = 1;
    }

    return 0;
}

void drv_i2c_inst_destroy(drv_i2c_inst_t** inst)
{
    int id, type, fd;
    if (NULL == (*inst)) {
        return;
    }

    if ((void*)&i2c_master_inst_type != (*inst)->base) {
        printf("[hal_i2c]: inst not i2c master\n");
        return;
    }

    type = (*inst)->type;
    fd   = (*inst)->fd;
    id   = (*inst)->id;

    free(*inst);
    *inst = NULL;

    if (0 <= fd) {
        close(fd);
    }

    if (DRV_I2C_TYPE_SOFT == type) {
        uint32_t bus_num = id;

        if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_DELETE_SOFT_I2C, &bus_num)) {
            printf("[hal_i2c]: can't delete soft i2c device");
        }
    } else {
        if (KD_HARD_I2C_MAX_NUM > id) {
            hard_i2c_in_use[id] = 0;
        }
    }
}

int drv_i2c_set_7b_addr(drv_i2c_inst_t* inst)
{
    if (!inst) {
        return -1;
    }

    return drv_i2c_ioctl(inst, RT_I2C_DEV_CTRL_7BIT, NULL);
}

int drv_i2c_set_10b_addr(drv_i2c_inst_t* inst)
{
    if (!inst) {
        return -1;
    }

    return drv_i2c_ioctl(inst, RT_I2C_DEV_CTRL_10BIT, NULL);
}

int drv_i2c_set_freq(drv_i2c_inst_t* inst, uint32_t freq)
{
    uint32_t _freq = freq;

    if (!inst) {
        return -1;
    }

    if (_freq == inst->freq) {
        return 0;
    }

    if (0x00 != drv_i2c_ioctl(inst, RT_I2C_DEV_CTRL_CLK, &_freq)) {
        printf("[hal_i2c]: i2c set clock failed\n");
        return -1;
    }

    inst->freq = _freq;

    return 0;
}

int drv_i2c_set_timeout(drv_i2c_inst_t* inst, uint32_t timeout_ms)
{
    uint32_t _timeout_ms = timeout_ms;

    if (!inst) {
        return -1;
    }

    if (_timeout_ms == inst->timeout_ms) {
        return 0;
    }

    if (0x00 != drv_i2c_ioctl(inst, RT_I2C_DEV_CTRL_TIMEOUT, &_timeout_ms)) {
        printf("[hal_i2c]: i2c set timeout failed\n");
        return -1;
    }

    inst->timeout_ms = _timeout_ms;

    return 0;
}

int drv_i2c_transfer(drv_i2c_inst_t* inst, i2c_msg_t* msgs, int msg_cnt)
{
    struct rt_i2c_priv_data {
        i2c_msg_t* msgs;
        size_t     number;
    } priv;

    if (!inst) {
        return -1;
    }

    priv.msgs   = msgs;
    priv.number = msg_cnt;

    if (0x00 == msg_cnt) {
        return 0;
    }

    return drv_i2c_ioctl(inst, RT_I2C_DEV_CTRL_RW, &priv);
}

// /** drv_i2c default pins *****************************************************/
// #include "generated/autoconf.h"

// static const drv_i2c_pin_cfg_t drv_i2c_dft_pins[KD_HARD_I2C_MAX_NUM] = {
//     {
//         .pin_scl = CONFIG_RTSMART_HAL_DRV_I2C0_PIN_SCL_DFT,
//         .pin_sda = CONFIG_RTSMART_HAL_DRV_I2C0_PIN_SDA_DFT,
//     },
//     {
//         .pin_scl = CONFIG_RTSMART_HAL_DRV_I2C1_PIN_SCL_DFT,
//         .pin_sda = CONFIG_RTSMART_HAL_DRV_I2C1_PIN_SDA_DFT,
//     },
//     {
//         .pin_scl = CONFIG_RTSMART_HAL_DRV_I2C2_PIN_SCL_DFT,
//         .pin_sda = CONFIG_RTSMART_HAL_DRV_I2C2_PIN_SDA_DFT,
//     },
//     {
//         .pin_scl = CONFIG_RTSMART_HAL_DRV_I2C3_PIN_SCL_DFT,
//         .pin_sda = CONFIG_RTSMART_HAL_DRV_I2C3_PIN_SDA_DFT,
//     },
//     {
//         .pin_scl = CONFIG_RTSMART_HAL_DRV_I2C4_PIN_SCL_DFT,
//         .pin_sda = CONFIG_RTSMART_HAL_DRV_I2C4_PIN_SDA_DFT,
//     },
// };

// int drv_i2c_get_default_pin(int id, drv_i2c_pin_cfg_t* pins)
// {
//     if ((0 > id) || (KD_HARD_I2C_MAX_NUM <= id)) {
//         printf("[hal_i2c]: invalid i2c id\n");
//         return -1;
//     }

//     if (pins) {
//         memcpy(pins, &drv_i2c_dft_pins[id], sizeof(drv_i2c_pin_cfg_t));
//     }

//     return 0;
// }

// int drv_i2c_install_pins(int id, drv_i2c_pin_cfg_t* pins)
// {
//     uint8_t      pin_scl, pin_sda;
//     fpioa_func_t func_scl, func_sda;

//     if ((0 > id) || (KD_HARD_I2C_MAX_NUM <= id) || (NULL == pins)) {
//         printf("[hal_i2c]: invalid i2c id\n");
//         return -1;
//     }

//     pin_scl = pins->pin_scl;
//     pin_sda = pins->pin_sda;

//     if ((0x00 != drv_fpioa_get_pin_func(pin_scl, &func_scl)) || (0x00 != drv_fpioa_get_pin_func(pin_scl, &func_sda))) {
//         printf("[hal_i2c]: get pin func failed.\n");
//         return -2;
//     }

//     if ((GPIO63 < func_scl) && (func_scl != (IIC0_SCL + id * 2))) {
//         printf("[hal_i2c]: pin %d can not set to scl\n", pin_scl);
//         return -3;
//     }

//     if ((GPIO63 < func_sda) && (func_sda != (IIC0_SDA + id * 2))) {
//         printf("[hal_i2c]: pin %d can not set to sda\n", pin_sda);
//         return -3;
//     }

//     if (GPIO63 < func_scl) {
//         if (0x00 != drv_fpioa_set_pin_func(pin_scl, (IIC0_SCL + id * 2))) {
//             printf("[hal_i2c]: set pin %d to scl failed\n", pin_scl);
//             return -4;
//         }
//     }

//     if (GPIO63 < func_sda) {
//         if (0x00 != drv_fpioa_set_pin_func(pin_sda, (IIC0_SDA + id * 2))) {
//             printf("[hal_i2c]: set pin %d to sda failed\n", pin_sda);
//             return -4;
//         }
//     }

//     return 0;
// }
