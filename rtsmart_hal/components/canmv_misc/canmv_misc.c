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

#include <stdio.h>
#include <string.h>

#include "canmv_misc.h"

int canmv_misc_get_sys_heap_size(struct canmv_misc_dev_meminfo_t* meminfo)
{
    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_READ_HEAP, meminfo)) {
        return -1;
    }

    return 0;
}

int canmv_misc_get_sys_page_info(struct canmv_misc_dev_meminfo_t* meminfo)
{
    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_READ_PAGE, meminfo)) {
        return -1;
    }

    return 0;
}

int canmv_misc_get_sys_mmz_info(struct canmv_misc_dev_meminfo_t* meminfo)
{
    char buffer[231]; // dirty work, read data size 230 will recv the data we wanted.

    int total, used, free;
    memset(buffer, 0, sizeof(buffer));

    int fd = open("/proc/media-mem", O_RDONLY);
    if (0 > fd) {
        return -1;
    }
    read(fd, buffer, 230);
    close(fd);
    buffer[230] = 0;

    sscanf(buffer, "total:%d,used:%d,remain=%d", &total, &used, &free);

    meminfo->total_size = total;
    meminfo->used_size  = used;
    meminfo->free_size  = free;

    return 0;
}

int canmv_misc_get_sys_memory_size(uint64_t* size)
{
    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_GET_MEMORY_SIZE, size)) {
        return -1;
    }

    return 0;
}

int canmv_misc_get_cpu_usage(int* usage)
{
    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_CPU_USAGE, usage)) {
        return -1;
    }

    return 0;
}

int canmv_misc_create_soft_i2c_device(struct soft_i2c_configure* config)
{
    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_CREATE_SOFT_I2C, config)) {
        return -1;
    }

    return 0;
}

int canmv_misc_delete_soft_i2c_device(uint32_t bus_num)
{
    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_DELETE_SOFT_I2C, &bus_num)) {
        return -1;
    }

    return 0;
}

int canmv_misc_ntp_sync(void)
{
    int result = -1;

    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_NTP_SYNC, &result)) {
        return -1;
    }

    return result;
}

int canmv_misc_get_utc_timestamp(time_t* tm)
{
    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_GET_UTC_TIMESTAMP, tm)) {
        return -1;
    }

    return 0;
}

int canmv_misc_set_utc_timestamp(time_t tm)
{
    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_SET_UTC_TIMESTAMP, &tm)) {
        return -1;
    }

    return 0;
}

int canmv_misc_get_local_time(struct tm* local_time)
{
    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_GET_LOCAL_TIME, local_time)) {
        return -1;
    }

    return 0;
}

int canmv_misc_set_timezone(int32_t offset)
{
    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_SET_TIMEZONE, &offset)) {
        return -1;
    }

    return 0;
}

int canmv_misc_get_timezone(int32_t* offset)
{
    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_GET_TIMEZONE, offset)) {
        return -1;
    }

    return 0;
}

int canmv_misc_set_auto_exec_py_stage(int stage)
{
    if (stage < STAGE_NORMAL || stage >= STAGE_MAX) {
        return -1; // Invalid stage
    }

    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_SET_AUTO_EXEC_PY_STAGE, &stage)) {
        return -1;
    }

    return 0;
}

int canmv_misc_create_encoder_dev(struct encoder_dev_cfg_t* cfg)
{
    if (!cfg) {
        return -1;
    }

    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_CREATE_ROTARY_ENC_DEV, cfg)) {
        return -1;
    }

    return 0;
}

int canmv_misc_delete_encode_dev(int index)
{
    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_DELETE_ROTARY_ENC_DEV, &index)) {
        return -1;
    }

    return 0;
}

int canmv_misc_create_touch_device(struct drv_touch_config_t* cfg)
{
    if (!cfg) {
        return -1;
    }

    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_REGISTER_TOUCH_DEVICE, cfg)) {
        return -1;
    }

    return 0;
}

int canmv_misc_delete_touch_device(int index)
{
    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_UNREGISTER_TOUCH_DEVICE, &index)) {
        return -1;
    }

    return 0;
}

int canmv_misc_get_mmz_zone_info(size_t* start, size_t* end)
{
    struct mmz_zone_info_t {
        size_t mmz_start;
        size_t mmz_end;
    } info;

    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_GET_MMZ_ZONE_INFO, &info)) {
        return -1;
    }

    if (start) {
        *start = info.mmz_start;
    }

    if (end) {
        *end = info.mmz_end;
    }

    return 0;
}

bool canmv_misc_check_phys_in_mmz_zone(size_t addr, size_t size)
{
    static int    mmz_info_valid = 0;
    static size_t mmz_start, mmz_end;

    if (0x00 == mmz_info_valid) {
        if (0x00 != canmv_misc_get_mmz_zone_info(&mmz_start, &mmz_end)) {
            return false;
        }

        mmz_info_valid = 1;
    }

    if (addr < mmz_start) {
        return false;
    }

    if ((addr + size) > mmz_end) {
        return false;
    }

    return true;
}

/**
 * userspace function to get kernel build info
 * @param out_buf: buffer to store the version string
 * @param buf_len: size of out_buf
 * @return: 0 on success, -1 on failure
 */
int canmv_misc_get_kernel_build_info(char* out_buf, size_t buf_len)
{
#define INFO_MAX_LEN (256)

    struct kernel_build_info_t {
        int  len;
        char info[0];
    };

    size_t alloc_size = sizeof(struct kernel_build_info_t) + INFO_MAX_LEN;

    struct kernel_build_info_t* k_info = (struct kernel_build_info_t*)malloc(alloc_size);

    if (!k_info) {
        return -1;
    }

    if (0x00 != canmv_misc_dev_ioctl(MISC_DEV_CMD_GET_KERNEL_BUILD_INFO, k_info)) {
        free(k_info);
        return -1;
    }

    if (out_buf && buf_len > 0) {
        size_t copy_len = (k_info->len < (int)buf_len) ? k_info->len : (buf_len - 1);
        memcpy(out_buf, k_info->info, copy_len);
        out_buf[copy_len] = '\0';
    }

    free(k_info);
    return 0;

#undef INFO_MAX_LEN
}

int canmv_misc_mkfs(const char* mount_path)
{
    struct canmv_misc_mkfs_args args = {0};
    size_t mount_path_len;

    if (!mount_path) {
        errno = EINVAL;
        return -1;
    }

    mount_path_len = strnlen(mount_path, sizeof(args.mount_path));
    if (!mount_path_len) {
        errno = EINVAL;
        return -1;
    }
    if (mount_path_len == sizeof(args.mount_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(args.mount_path, mount_path, mount_path_len);
    return canmv_misc_dev_ioctl(MISC_DEV_CMD_MKFS, &args);
}
