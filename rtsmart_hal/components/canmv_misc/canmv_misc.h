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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MISC_DEV_CMD_READ_HEAP               _IOWR('M', 0x00, void*)
#define MISC_DEV_CMD_READ_PAGE               _IOWR('M', 0x01, void*)
#define MISC_DEV_CMD_GET_MEMORY_SIZE         _IOWR('M', 0x02, void*)
#define MISC_DEV_CMD_CPU_USAGE               _IOWR('M', 0x03, void*)
#define MISC_DEV_CMD_CREATE_SOFT_I2C         _IOWR('M', 0x04, void*)
#define MISC_DEV_CMD_DELETE_SOFT_I2C         _IOWR('M', 0x05, void*)
#define MISC_DEV_CMD_NTP_SYNC                _IOWR('M', 0x07, void*)
#define MISC_DEV_CMD_GET_UTC_TIMESTAMP       _IOWR('M', 0x08, void*)
#define MISC_DEV_CMD_SET_UTC_TIMESTAMP       _IOWR('M', 0x09, void*)
#define MISC_DEV_CMD_GET_LOCAL_TIME          _IOWR('M', 0x0a, void*)
#define MISC_DEV_CMD_SET_TIMEZONE            _IOWR('M', 0x0b, void*)
#define MISC_DEV_CMD_GET_TIMEZONE            _IOWR('M', 0x0c, void*)
#define MISC_DEV_CMD_SET_AUTO_EXEC_PY_STAGE  _IOWR('M', 0x0d, void*)
#define MISC_DEV_CMD_CREATE_ROTARY_ENC_DEV   _IOWR('M', 0x0e, void*)
#define MISC_DEV_CMD_DELETE_ROTARY_ENC_DEV   _IOWR('M', 0x0f, void*)
#define MISC_DEV_CMD_REGISTER_TOUCH_DEVICE   _IOWR('M', 0x10, void*)
#define MISC_DEV_CMD_UNREGISTER_TOUCH_DEVICE _IOWR('M', 0x11, void*)
#define MISC_DEV_CMD_GET_MMZ_ZONE_INFO       _IOWR('M', 0x12, void*)

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// MISC_DEV_CMD_READ_HEAP
// MISC_DEV_CMD_READ_PAGE
struct canmv_misc_dev_meminfo_t {
    size_t total_size;
    size_t free_size;
    size_t used_size;
};

// MISC_DEV_CMD_CREATE_SOFT_I2C
struct soft_i2c_configure {
    uint32_t bus_num;
    uint32_t pin_scl;
    uint32_t pin_sda;
    uint32_t freq;
    uint32_t timeout_ms;
};

// MISC_DEV_CMD_SET_AUTO_EXEC_PY_STAGE
enum {
    STAGE_NORMAL = 1,
    STAGE_BOOTPY_START,
    STAGE_BOOTPY_END,
    STAGE_MAINPY_START,
    STAGE_MAINPY_END,
    STAGE_FALLBACK_PY_START,
    STAGE_FALLBACK_PY_END,
    STAGE_MAX,
};

// MISC_DEV_CMD_CREATE_ROTARY_ENC_DEV
struct encoder_pin_cfg_t {
    int clk_pin; /* Clock/A phase pin */
    int dt_pin; /* Data/B phase pin */
    int sw_pin; /* Switch/button pin (use -1 if not connected) */
};
struct encoder_dev_cfg_t {
    int index;

    struct encoder_pin_cfg_t cfg;
};

struct drv_touch_config_t {
    int touch_dev_index;

    int range_x;
    int range_y;

    int pin_intr;
    int intr_value;
    int pin_reset;
    int reset_value;

    int i2c_bus_index;
    int i2c_bus_speed;
};

static inline __attribute__((always_inline)) int canmv_misc_dev_ioctl(int cmd, void* args)
{
    int result = 0, misc_dev_fd = -1;

    if (0 > (misc_dev_fd = open("/dev/canmv_misc", O_RDWR))) {
        printf("can not misc device");
        return -1;
    }

    if (0x00 != (result = ioctl(misc_dev_fd, cmd, args))) {
        printf("ioctl misc device failed, cmd %x\n", cmd);
    }

    if (0 <= misc_dev_fd) {
        close(misc_dev_fd);
    }

    return result;
}

int canmv_misc_get_sys_heap_size(struct canmv_misc_dev_meminfo_t* meminfo);
int canmv_misc_get_sys_page_info(struct canmv_misc_dev_meminfo_t* meminfo);
int canmv_misc_get_sys_mmz_info(struct canmv_misc_dev_meminfo_t* meminfo);

int canmv_misc_get_sys_memory_size(uint64_t* size);

int canmv_misc_get_cpu_usage(int* usage);

int canmv_misc_create_soft_i2c_device(struct soft_i2c_configure* config);
int canmv_misc_delete_soft_i2c_device(uint32_t bus_num);

int canmv_misc_ntp_sync(void);

int canmv_misc_get_utc_timestamp(time_t* tm);
int canmv_misc_set_utc_timestamp(time_t tm);

int canmv_misc_get_local_time(struct tm* local_time);

int canmv_misc_set_timezone(int32_t offset);
int canmv_misc_get_timezone(int32_t* offset);

int canmv_misc_set_auto_exec_py_stage(int stage);

int canmv_misc_create_encoder_dev(struct encoder_dev_cfg_t* cfg);
int canmv_misc_delete_encode_dev(int index);

int canmv_misc_create_touch_device(struct drv_touch_config_t* cfg);
int canmv_misc_delete_touch_device(int index);

int canmv_misc_get_mmz_zone_info(size_t* start, size_t* end);

bool canmv_misc_check_phys_in_mmz_zone(size_t addr, size_t size);

#ifdef __cplusplus
}
#endif /* __cplusplus */
