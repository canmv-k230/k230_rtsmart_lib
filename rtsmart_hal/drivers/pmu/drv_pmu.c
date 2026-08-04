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

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "drv_pmu.h"

#ifndef _IOW
#define _IOC(a, b, c, d) (((a) << 30) | ((b) << 8) | (c) | ((d) << 16))
#define _IOC_NONE  0U
#define _IOC_WRITE 1U
#define _IOC_READ  2U
#define _IO(a, b) _IOC(_IOC_NONE, (a), (b), 0)
#define _IOW(a, b, c) _IOC(_IOC_WRITE, (a), (b), sizeof(c))
#define _IOR(a, b, c) _IOC(_IOC_READ, (a), (b), sizeof(c))
#define _IOWR(a, b, c) _IOC(_IOC_READ | _IOC_WRITE, (a), (b), sizeof(c))
#endif

#define DRV_PMU_DEV_PATH                        "/dev/pmu_pwrkey"
#define DRV_PMU_DEFAULT_SIGNO                   SIGUSR1
struct drv_pmu_notify_cfg {
    int32_t pid;
    int32_t signo;
};

struct drv_pmu_shutdown_request {
    uint32_t pending;
};

struct drv_pmu_power_cycle_cfg {
    uint32_t shutdown_after_s;
    uint32_t poweron_after_s;
    uint32_t flags;
    uint32_t reserved;
};

struct drv_pmu_wakeup_pad_level {
    int32_t level;
};

#define PMU_IOCTL_REGISTER_NOTIFY \
    _IOW('P', 0x00, struct drv_pmu_notify_cfg)
#define PMU_IOCTL_UNREGISTER_NOTIFY \
    _IO('P', 0x01)
#define PMU_IOCTL_GET_SHUTDOWN_REQUEST \
    _IOR('P', 0x02, struct drv_pmu_shutdown_request)
#define PMU_IOCTL_CONFIRM_SHUTDOWN \
    _IO('P', 0x03)
#define PMU_IOCTL_SCHEDULE_POWER_CYCLE \
    _IOW('P', 0x04, struct drv_pmu_power_cycle_cfg)
#define PMU_IOCTL_CANCEL_POWER_CYCLE \
    _IO('P', 0x05)
#define PMU_IOCTL_SHUTDOWN_NOW \
    _IO('P', 0x06)
#define PMU_IOCTL_GET_WAKEUP_PAD_LEVEL \
    _IOR('P', 0x07, struct drv_pmu_wakeup_pad_level)

struct drv_pmu_inst {
    int fd;
    int signo;
    bool notify_registered;
    bool signal_was_blocked;
    sigset_t waitset;
};

static int drv_pmu_is_open(const drv_pmu_inst_t *inst)
{
    if ((inst != NULL) && (inst->fd >= 0))
        return 1;

    errno = EINVAL;
    return 0;
}

static int drv_pmu_ioctl(drv_pmu_inst_t *inst, unsigned long request,
                         void *arg, const char *name)
{
    if (!drv_pmu_is_open(inst))
        return -1;

    if (ioctl(inst->fd, request, arg) < 0) {
        perror(name);
        return -1;
    }

    return 0;
}

static int drv_pmu_wait_signal(drv_pmu_inst_t *inst, int timeout_ms)
{
    siginfo_t info;
    int ret;

    memset(&info, 0, sizeof(info));

    if (timeout_ms < 0) {
        ret = sigwaitinfo(&inst->waitset, &info);
    } else {
        struct timespec timeout = {
            .tv_sec = timeout_ms / 1000,
            .tv_nsec = (timeout_ms % 1000) * 1000000L,
        };

        ret = sigtimedwait(&inst->waitset, &info, &timeout);
    }

    if (ret >= 0)
        return 0;

    if ((errno == EAGAIN) || (errno == EINTR))
        return 1;

    perror("[hal_pmu] sigwait");
    return -1;
}

static int drv_pmu_read_shutdown_request(drv_pmu_inst_t *inst)
{
    struct drv_pmu_shutdown_request request;

    memset(&request, 0, sizeof(request));
    if (drv_pmu_ioctl(inst, PMU_IOCTL_GET_SHUTDOWN_REQUEST, &request,
              "[hal_pmu] ioctl(GET_SHUTDOWN_REQUEST)") < 0)
        return -1;

    return request.pending != 0U ? 0 : 1;
}

static int drv_pmu_block_signal(drv_pmu_inst_t *inst, int signo)
{
    sigset_t oldset;
    int ret;

    sigemptyset(&inst->waitset);
    sigaddset(&inst->waitset, signo);

    ret = pthread_sigmask(SIG_BLOCK, &inst->waitset, &oldset);
    if (ret != 0) {
        errno = ret;
        perror("[hal_pmu] pthread_sigmask(SIG_BLOCK)");
        return -1;
    }

    inst->signal_was_blocked = sigismember(&oldset, signo) == 1;
    inst->signo = signo;

    return 0;
}

static void drv_pmu_unblock_signal(drv_pmu_inst_t *inst)
{
    int ret;

    if (inst->signo <= 0)
        return;

    if (!inst->signal_was_blocked) {
        ret = pthread_sigmask(SIG_UNBLOCK, &inst->waitset, NULL);
        if (ret != 0) {
            errno = ret;
            perror("[hal_pmu] pthread_sigmask(SIG_UNBLOCK)");
        }
    }

    sigemptyset(&inst->waitset);
    inst->signal_was_blocked = false;
    inst->signo = 0;
}

int drv_pmu_inst_create(drv_pmu_inst_t **inst)
{
    drv_pmu_inst_t *pmu;
    int flags;

    if (inst == NULL)
        return -1;

    if (*inst != NULL)
        drv_pmu_inst_destroy(inst);

    pmu = calloc(1, sizeof(*pmu));
    if (pmu == NULL) {
        perror("[hal_pmu] calloc");
        return -1;
    }

    pmu->fd = -1;

    flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif

    pmu->fd = open(DRV_PMU_DEV_PATH, flags);
    if (pmu->fd < 0) {
        perror("[hal_pmu] open");
        free(pmu);
        return -1;
    }

    *inst = pmu;
    return 0;
}

void drv_pmu_inst_destroy(drv_pmu_inst_t **inst)
{
    drv_pmu_inst_t *pmu;

    if ((inst == NULL) || (*inst == NULL))
        return;

    pmu = *inst;

    drv_pmu_key_unregister_notify(pmu);

    if (pmu->fd >= 0) {
        close(pmu->fd);
        pmu->fd = -1;
    }

    free(pmu);
    *inst = NULL;
}

int drv_pmu_key_register_notify(drv_pmu_inst_t *inst, int signo)
{
    struct drv_pmu_notify_cfg cfg = {
        .pid = 0,
        .signo = signo > 0 ? signo : DRV_PMU_DEFAULT_SIGNO,
    };

    if (!drv_pmu_is_open(inst))
        return -1;

    if (inst->notify_registered) {
        if (drv_pmu_key_unregister_notify(inst) < 0)
            return -1;
    }

    if (drv_pmu_block_signal(inst, cfg.signo) < 0)
        return -1;

    if (drv_pmu_ioctl(inst, PMU_IOCTL_REGISTER_NOTIFY, &cfg,
              "[hal_pmu] ioctl(REGISTER_NOTIFY)") < 0) {
        drv_pmu_unblock_signal(inst);
        return -1;
    }

    inst->notify_registered = true;
    return 0;
}

int drv_pmu_key_unregister_notify(drv_pmu_inst_t *inst)
{
    int ret = 0;

    if (!drv_pmu_is_open(inst))
        return -1;

    if (!inst->notify_registered)
        return 0;

    if (drv_pmu_ioctl(inst, PMU_IOCTL_UNREGISTER_NOTIFY, NULL,
              "[hal_pmu] ioctl(UNREGISTER_NOTIFY)") < 0) {
        ret = -1;
    }

    drv_pmu_unblock_signal(inst);
    inst->notify_registered = false;

    return ret;
}

int drv_pmu_key_wait_shutdown(drv_pmu_inst_t *inst, int timeout_ms)
{
    int ret;

    if ((inst == NULL) || !inst->notify_registered)
        return -1;

    ret = drv_pmu_wait_signal(inst, timeout_ms);
    if (ret != 0)
        return ret;

    ret = drv_pmu_read_shutdown_request(inst);
    return ret;
}

int drv_pmu_key_confirm_shutdown(drv_pmu_inst_t *inst)
{
    return drv_pmu_ioctl(inst, PMU_IOCTL_CONFIRM_SHUTDOWN, NULL,
                 "[hal_pmu] ioctl(CONFIRM_SHUTDOWN)");
}

int drv_pmu_rtc_schedule_power_cycle(drv_pmu_inst_t *inst,
                                     uint32_t shutdown_after_s,
                                     uint32_t poweron_after_s)
{
    struct drv_pmu_power_cycle_cfg cfg = {
        .shutdown_after_s = shutdown_after_s,
        .poweron_after_s = poweron_after_s,
        .flags = 0,
        .reserved = 0,
    };

    return drv_pmu_ioctl(inst, PMU_IOCTL_SCHEDULE_POWER_CYCLE, &cfg,
                 "[hal_pmu] ioctl(SCHEDULE_POWER_CYCLE)");
}

int drv_pmu_rtc_cancel_power_cycle(drv_pmu_inst_t *inst)
{
    return drv_pmu_ioctl(inst, PMU_IOCTL_CANCEL_POWER_CYCLE, NULL,
                 "[hal_pmu] ioctl(CANCEL_POWER_CYCLE)");
}

int drv_pmu_shutdown_now(drv_pmu_inst_t *inst)
{
    return drv_pmu_ioctl(inst, PMU_IOCTL_SHUTDOWN_NOW, NULL,
                 "[hal_pmu] ioctl(SHUTDOWN_NOW)");
}

int drv_pmu_wakeup_pad_get_level(drv_pmu_inst_t *inst, int *level)
{
    struct drv_pmu_wakeup_pad_level result;

    if (level == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(&result, 0, sizeof(result));
    if (drv_pmu_ioctl(inst, PMU_IOCTL_GET_WAKEUP_PAD_LEVEL, &result,
              "[hal_pmu] ioctl(GET_WAKEUP_PAD_LEVEL)") < 0)
        return -1;

    if ((result.level != 0) && (result.level != 1)) {
        errno = EIO;
        return -1;
    }

    *level = result.level;
    return 0;
}
