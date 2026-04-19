/* Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "drv_pufs.h"
#include "hal_rvv_ops.h"

#define PUFS_BUF_SIZE  65536

int drv_pufs_dev_lock(drv_pufs_inst *inst)
{
    if (!inst || !inst->io_lock_init)
        return -1;

    return pthread_mutex_lock(&inst->io_lock);
}

void drv_pufs_dev_unlock(drv_pufs_inst *inst)
{
    if (inst && inst->io_lock_init)
        pthread_mutex_unlock(&inst->io_lock);
}

int drv_pufs_ioctl(int fd, unsigned long request, void *arg)
{
    int ret;

    errno = 0;
    ret = ioctl(fd, request, arg);
    if (ret == -1 && errno != 0)
        return -errno;

    return ret;
}

/* ===== Device management ===== */

int drv_pufs_open(drv_pufs_inst *inst)
{
    if (!inst)
        return -1;

    hal_rvv_memset(inst, 0, sizeof(*inst));
    inst->fd = -1;

    inst->fd = open("/dev/pufs", O_RDWR);
    if (inst->fd < 0)
        return -1;

    if (pthread_mutex_init(&inst->io_lock, NULL) != 0)
        goto fail;
    inst->io_lock_init = 1;

    inst->buf_size = PUFS_BUF_SIZE;
    return 0;

fail:
    if (inst->io_lock_init) {
        pthread_mutex_destroy(&inst->io_lock);
        inst->io_lock_init = 0;
    }
    if (inst->fd >= 0)
        close(inst->fd);
    inst->fd = -1;
    return -1;
}

int drv_pufs_close(drv_pufs_inst *inst)
{
    if (!inst)
        return -1;

    if (inst->io_lock_init) {
        pthread_mutex_destroy(&inst->io_lock);
        inst->io_lock_init = 0;
    }
    if (inst->fd >= 0)
        close(inst->fd);
    inst->fd = -1;
    return 0;
}
