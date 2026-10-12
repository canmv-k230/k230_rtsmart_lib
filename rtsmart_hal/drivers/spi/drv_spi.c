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
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "drv_spi.h"
#include "drv_fpioa.h"
#include "drv_gpio.h"

/* SPI control commands */
#define RT_SPI_DEV_CTRL_CONFIG      (12 * 0x100 + 0x01)
#define RT_SPI_DEV_CTRL_RW          (12 * 0x100 + 0x02)
/* Chip select types */
#define SPI_CS_ACTIVE_HIGH (1<<4)       /* Chipselect active high */

#define CS_ACTIVE (0x1)
#define CS_INACTIVE (0x0)

struct drv_spi_inst {
    uint64_t handle_id;
    int dev_fd;
    uint8_t spi_id;
    uint8_t mode;
    uint32_t baudrate;
    uint8_t data_bits;
    int cs_pin;
    drv_gpio_inst_t *gpio_cs;
    uint8_t data_line;
    uint8_t cs_status;
    bool use_hw_cs;
};

struct rt_spi_configuration {
    uint8_t mode;
    uint8_t data_width;
    union {
        struct {
            /* hard spi cs configure */
            uint16_t hard_cs : 8;
            /* 0x80 | pin */
            uint16_t soft_cs : 8;
        };
        uint16_t reserved;
    };
    uint32_t max_hz;
};

struct rt_qspi_configuration {
    struct rt_spi_configuration parent;
    /* The size of medium */
    uint32_t medium_size;
    /* double data rate mode */
    uint8_t ddr_mode;
    /* the data lines max width which QSPI bus supported, such as 1, 2, 4 */
    uint8_t qspi_dl_width;
};

static uint64_t g_handle_id[SPI_HAL_MAX_DEVICES];
static pthread_spinlock_t lock[SPI_HAL_MAX_DEVICES];

int drv_spi_inst_create(int spi_id, bool active_low, int mode, uint32_t baudrate,
                        uint8_t data_bits, int cs_pin, uint8_t data_line, drv_spi_inst_t *inst)
{
    int ret = -1;
    static uint64_t idx;

    if (spi_id < 0 || spi_id >= SPI_HAL_MAX_DEVICES) {
        printf("[hal_spi]: invalid spi id, range in [0 ~ %d]\n", SPI_HAL_MAX_DEVICES - 1);
        goto err_0;
    }

    if (mode < SPI_HAL_MODE_0 || mode > SPI_HAL_MODE_3) {
        printf("[hal_spi]: invalid spi mode, range in [%d ~ %d]\n", SPI_HAL_MODE_0, SPI_HAL_MODE_3);
        goto err_0;
    }

    if (baudrate == 0) {
        printf("[hal_spi]: baudrate must be greater than zero\n");
        goto err_0;
    }

    if (data_bits < 4 || data_bits > 32) {
        printf("[hal_spi]: invalid data bits, range in [4 ~ 32]\n");
        goto err_0;
    }

    if (data_line != SPI_HAL_DATA_LINE_1 && data_line != SPI_HAL_DATA_LINE_2 &&
        data_line != SPI_HAL_DATA_LINE_4 && data_line != SPI_HAL_DATA_LINE_8) {
        printf("[hal_spi]: invalid data line, only support 1/2/4 line spi\n");
        goto err_0;
    }

    if (data_line == SPI_HAL_DATA_LINE_8 && spi_id != 0) {
        printf("[hal_spi]: invalid data line, only spi0 support 8 line spi\n");
        goto err_0;
    }

    if ((cs_pin < 0 || cs_pin > 63) && (cs_pin != -1)) {
        printf("[hal_spi]: invalid soft cs (0x%x),range in [0x%x ~ 0x%x] OR -1\n",
               cs_pin, 0, 63);
        goto err_0;
    }

    if (inst == NULL) {
        printf("[hal_spi]: NULL output pointer\n");
        goto err_0;
    }
    *inst = NULL;

    *inst = malloc(sizeof(struct drv_spi_inst));
    if (!(*inst)) {
        printf("[hal_spi]: malloc drv_spi_inst fail\n");
        goto err_0;
    }

    memset(*inst, 0, sizeof(struct drv_spi_inst));
    (*inst)->spi_id = spi_id;

    if (cs_pin != -1) {
        if (drv_fpioa_set_pin_func(cs_pin, GPIO0 + cs_pin) != 0) {
            printf("[hal_spi]: fail to set cs pin func\n");
            goto err_1;
        }

        if (drv_gpio_inst_create(cs_pin, &(*inst)->gpio_cs) != 0) {
            printf("[hal_spi]: failed to create gpio instance for cs pin\n");
            goto err_1;
        }
        if (drv_gpio_mode_set((*inst)->gpio_cs, GPIO_DM_OUTPUT) != 0) {
            printf("[hal_spi]: failed to set cs pin mode\n");
            goto err_2;
        }

        if (!active_low) {
            mode |= SPI_CS_ACTIVE_HIGH;
            drv_gpio_value_set((*inst)->gpio_cs, GPIO_PV_LOW);
        } else {
            drv_gpio_value_set((*inst)->gpio_cs, GPIO_PV_HIGH);
        }
        (*inst)->use_hw_cs = true;
    }

    (*inst)->mode = mode;
    (*inst)->baudrate = baudrate;
    (*inst)->data_bits = data_bits;
    (*inst)->cs_pin = cs_pin;
    (*inst)->data_line = data_line;
    (*inst)->cs_status = CS_INACTIVE;

    char dev_name[32];
    snprintf(dev_name, sizeof(dev_name), "/dev/spi%d", spi_id);

    (*inst)->dev_fd = open(dev_name, O_RDWR);
    if ((*inst)->dev_fd < 0) {
        printf("[hal_spi]: open %s fail in function %s\n", dev_name, __func__);
        goto err_2;
    }

    struct rt_qspi_configuration spi_config;
    memset(&spi_config, 0, sizeof(spi_config));

    spi_config.parent.mode = mode;
    spi_config.parent.data_width = data_bits;

    spi_config.parent.hard_cs = 0;
    if (cs_pin != -1) {
        spi_config.parent.soft_cs = cs_pin | 0x80;
    } else {
        spi_config.parent.soft_cs = 0;
    }

    spi_config.parent.max_hz = baudrate;
    spi_config.qspi_dl_width = data_line;
    (*inst)->handle_id = idx++;

    if ((ret = ioctl((*inst)->dev_fd, RT_SPI_DEV_CTRL_CONFIG, &spi_config))) {
        printf("[hal_spi]: spi config fail: %s (errno: %d, ret: %d)\n", strerror(errno), errno, ret);
        goto err_3;
    }
    pthread_spin_lock(&lock[spi_id]);
    g_handle_id[spi_id] = (*inst)->handle_id;
    pthread_spin_unlock(&lock[spi_id]);

    ret = 0;
    return ret;

err_3:
    close((*inst)->dev_fd);
err_2:
    if (cs_pin != -1) {
        drv_gpio_inst_destroy(&(*inst)->gpio_cs);
    }
err_1:
    free(*inst);
    *inst = NULL;
err_0:

    return ret;
}

void drv_spi_inst_destroy(drv_spi_inst_t *inst)
{
    if (inst != NULL) {
        if (*inst) {

            if ((*inst)->gpio_cs) {
                drv_gpio_inst_destroy(&(*inst)->gpio_cs);
                (*inst)->gpio_cs = NULL;
            }

            if ((*inst)->dev_fd >= 0) {
                close((*inst)->dev_fd);
            }
            free((*inst));
            *inst = NULL;
        }
    }
}

int drv_spi_transfer(drv_spi_inst_t inst, const void *tx_data,
                        void *rx_data, size_t len, bool cs_change)
{
    int ret = 0;
    int cs_pin;

    struct rt_qspi_message msg;

    if (!inst || inst->dev_fd < 0) {
        printf("[hal_spi]: pls drv_spi_inst_create first\n");
        ret = -1;
        goto out;
    }

    if (len == 0) {
        goto out;
    }

    if (len > INT_MAX || (tx_data == NULL && rx_data == NULL)) {
        printf("[hal_spi]: invalid transfer buffer or length\n");
        ret = -1;
        goto out;
    }

    pthread_spin_lock(&lock[inst->spi_id]);
    cs_pin = inst->cs_pin;
    if (g_handle_id[inst->spi_id] != inst->handle_id) {
        struct rt_qspi_configuration spi_config;
        memset(&spi_config, 0, sizeof(spi_config));

        spi_config.parent.mode = inst->mode;
        spi_config.parent.data_width = inst->data_bits;

        spi_config.parent.hard_cs = 0;
        if (cs_pin != -1) {
            spi_config.parent.soft_cs = cs_pin | 0x80;
        } else {
            spi_config.parent.soft_cs = 0;
        }

        spi_config.parent.max_hz = inst->baudrate;
        spi_config.qspi_dl_width = inst->data_line;

        if ((ret = ioctl(inst->dev_fd, RT_SPI_DEV_CTRL_CONFIG, &spi_config))) {
            printf("[hal_spi]: spi config fail: %s (errno: %d, ret: %d)\n", strerror(errno), errno, ret);
            pthread_spin_unlock(&lock[inst->spi_id]);
            goto out;
        }
        g_handle_id[inst->spi_id] = inst->handle_id;
    }
    pthread_spin_unlock(&lock[inst->spi_id]);

    memset(&msg, 0, sizeof(msg));

    msg.parent.send_buf = tx_data;
    msg.parent.recv_buf = rx_data;
    msg.parent.length = len;
    msg.parent.next = NULL;

    if (cs_pin != -1) {
        if (inst->cs_status == CS_ACTIVE) {
            msg.parent.cs_take = 0;
        } else {
            msg.parent.cs_take = 1;
            inst->cs_status = CS_ACTIVE;
        }

        if (cs_change) {
            msg.parent.cs_release = 1;
            inst->cs_status = CS_INACTIVE;
        } else {
            msg.parent.cs_release = 0;
        }
    } else {
        msg.parent.cs_take = 0;
        msg.parent.cs_release = 0;
    }

    msg.qspi_data_lines = inst->data_line;

    ret = ioctl(inst->dev_fd, RT_SPI_DEV_CTRL_RW, &msg);
    if (ret != (int)len) {
        printf("[hal_spi]: spi transfer fail: %s (errno: %d, ret: %d)\n", strerror(errno), errno, ret);
    }

out:
    return ret;
}

int drv_spi_read(drv_spi_inst_t inst, void *rx_data, size_t len, bool cs_change)
{
    return drv_spi_transfer(inst, NULL, rx_data, len, cs_change);
}

int drv_spi_write(drv_spi_inst_t inst, const void *tx_data, size_t len, bool cs_change)
{
    return drv_spi_transfer(inst, tx_data, NULL, len, cs_change);
}

int drv_spi_transfer_message(drv_spi_inst_t inst, struct rt_qspi_message *msg)
{
    int ret;
    int cs_pin;

    if (!inst || inst->dev_fd < 0 || msg == NULL || msg->parent.length > INT_MAX) {
        printf("[hal_spi]: pls drv_spi_inst_create first\n");
        ret = -1;
        goto out;
    }

    pthread_spin_lock(&lock[inst->spi_id]);
    cs_pin = inst->cs_pin;
    if (g_handle_id[inst->spi_id] != inst->handle_id) {
        struct rt_qspi_configuration spi_config;
        memset(&spi_config, 0, sizeof(spi_config));

        spi_config.parent.mode = inst->mode;
        spi_config.parent.data_width = inst->data_bits;

        spi_config.parent.hard_cs = 0;
        if (cs_pin != -1) {
            spi_config.parent.soft_cs = cs_pin | 0x80;
        } else {
            spi_config.parent.soft_cs = 0;
        }

        spi_config.parent.max_hz = inst->baudrate;
        spi_config.qspi_dl_width = inst->data_line;

        if ((ret = ioctl(inst->dev_fd, RT_SPI_DEV_CTRL_CONFIG, &spi_config))) {
            printf("[hal_spi]: spi config fail: %s (errno: %d, ret: %d)\n", strerror(errno), errno, ret);
            pthread_spin_unlock(&lock[inst->spi_id]);
            goto out;
        }
        g_handle_id[inst->spi_id] = inst->handle_id;
    }
    pthread_spin_unlock(&lock[inst->spi_id]);

    msg->qspi_data_lines = inst->data_line;
    ret = ioctl(inst->dev_fd, RT_SPI_DEV_CTRL_RW, msg);
    if (ret != (int)msg->parent.length) {
        printf("[hal_spi]: spi transfer message fail: %s (errno: %d, ret: %d)\n", strerror(errno), errno, ret);
    }

out:
    return ret;
}

static int drv_spi_update_config(drv_spi_inst_t inst)
{
    int ret;
    struct rt_qspi_configuration spi_config;

    memset(&spi_config, 0, sizeof(spi_config));

    spi_config.parent.mode = inst->mode;
    spi_config.parent.data_width = inst->data_bits;

    spi_config.parent.hard_cs = 0;
    if (inst->cs_pin != -1 && inst->use_hw_cs) {
        spi_config.parent.soft_cs = inst->cs_pin | 0x80;
    } else if (!inst->use_hw_cs) {
        spi_config.parent.soft_cs = 0;
    } else {
        printf("pls offer hw cs pin\n");
        ret = -1;
        goto out;
    }

    spi_config.parent.max_hz = inst->baudrate;
    spi_config.qspi_dl_width = inst->data_line;

    if ((ret = ioctl(inst->dev_fd, RT_SPI_DEV_CTRL_CONFIG, &spi_config))) {
        printf("[hal_spi]: spi config fail: %s (errno: %d, ret: %d)\n", strerror(errno), errno, ret);
    } else {
        g_handle_id[inst->spi_id] = inst->handle_id;
    }

out:
    return ret;
}

int drv_spi_set_baudrate(drv_spi_inst_t inst, uint32_t baudrate)
{
    int ret = 0;
    uint32_t old_baudrate;

    if (!inst || inst->dev_fd < 0 || baudrate == 0) {
        printf("[hal_spi]: pls drv_spi_inst_create first\n");
        ret = -1;
        goto out;
    }

    pthread_spin_lock(&lock[inst->spi_id]);
    old_baudrate = inst->baudrate;
    inst->baudrate = baudrate;
    ret = drv_spi_update_config(inst);
    if (ret)
        inst->baudrate = old_baudrate;
    pthread_spin_unlock(&lock[inst->spi_id]);

out:
    return ret;
}

int drv_spi_set_datamode(drv_spi_inst_t inst, uint8_t mode)
{
    int ret = 0;
    uint8_t old_mode;

    if (!inst || inst->dev_fd < 0 || mode > SPI_HAL_MODE_3) {
        printf("[hal_spi]: pls drv_spi_inst_create first\n");
        ret = -1;
        goto out;
    }

    pthread_spin_lock(&lock[inst->spi_id]);
    old_mode = inst->mode;
    inst->mode = (inst->mode & SPI_CS_ACTIVE_HIGH) | mode;
    ret = drv_spi_update_config(inst);
    if (ret)
        inst->mode = old_mode;
    pthread_spin_unlock(&lock[inst->spi_id]);

out:
    return ret;
}

int drv_spi_set_cs_polarity(drv_spi_inst_t inst, bool is_active_hi)
{
    int ret = 0;
    uint8_t old_mode;

    if (!inst || inst->dev_fd < 0) {
        printf("[hal_spi]: pls drv_spi_inst_create first\n");
        ret = -1;
        goto out;
    }

    if (inst->cs_pin != -1) {
        pthread_spin_lock(&lock[inst->spi_id]);
        old_mode = inst->mode;
        if (is_active_hi) {
            inst->mode |= SPI_CS_ACTIVE_HIGH;
            drv_gpio_value_set(inst->gpio_cs, GPIO_PV_LOW);
        } else {
            inst->mode &= ~SPI_CS_ACTIVE_HIGH;
            drv_gpio_value_set(inst->gpio_cs, GPIO_PV_HIGH);
        }
        ret = drv_spi_update_config(inst);
        if (ret) {
            inst->mode = old_mode;
            drv_gpio_value_set(inst->gpio_cs,
                               old_mode & SPI_CS_ACTIVE_HIGH ? GPIO_PV_LOW
                                                             : GPIO_PV_HIGH);
        }
        pthread_spin_unlock(&lock[inst->spi_id]);
    }

out:
    return ret;
}

int drv_spi_set_cs_mode(drv_spi_inst_t inst, bool use_hw_cs)
{
    int ret = 0;
    bool old_mode;

    if (!inst || inst->dev_fd < 0) {
        printf("[hal_spi]: pls drv_spi_inst_create first\n");
        ret = -1;
        goto out;
    }

    if (use_hw_cs && inst->cs_pin == -1) {
        printf("pls offer hw cs pin\n");
        return -1;
    }

    pthread_spin_lock(&lock[inst->spi_id]);
    old_mode = inst->use_hw_cs;
    inst->use_hw_cs = use_hw_cs;
    ret = drv_spi_update_config(inst);
    if (ret)
        inst->use_hw_cs = old_mode;

    pthread_spin_unlock(&lock[inst->spi_id]);

out:
    return ret;
}

int drv_spi_set_data_bits(drv_spi_inst_t inst, uint8_t data_bits)
{
    int ret = 0;
    uint8_t old_data_bits;

    if (!inst || inst->dev_fd < 0 || data_bits < 4 || data_bits > 32) {
        printf("[hal_spi]: pls drv_spi_inst_create first\n");
        ret = -1;
        goto out;
    }

    pthread_spin_lock(&lock[inst->spi_id]);
    old_data_bits = inst->data_bits;
    inst->data_bits = data_bits;
    ret = drv_spi_update_config(inst);
    if (ret)
        inst->data_bits = old_data_bits;
    pthread_spin_unlock(&lock[inst->spi_id]);

out:
    return ret;
}
