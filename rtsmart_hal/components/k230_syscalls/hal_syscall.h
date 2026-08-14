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
#include <stdio.h>

#include <unistd.h>

/* clang-format off */

#define NRSYS(x) _NRSYS_##x,

enum
{
    _NRSYS_NONE = 0,

NRSYS(exit)            /* 01 */
NRSYS(read)
NRSYS(write)
NRSYS(lseek)
NRSYS(open)            /* 05 */
NRSYS(close)
NRSYS(ioctl)
NRSYS(fstat)
NRSYS(poll)
NRSYS(nanosleep)       /* 10 */
NRSYS(gettimeofday)
NRSYS(settimeofday)
NRSYS(exec)
NRSYS(kill)
NRSYS(getpid)          /* 15 */
NRSYS(getpriority)
NRSYS(setpriority)
NRSYS(sem_create)
NRSYS(sem_delete)
NRSYS(sem_take)        /* 20 */
NRSYS(sem_release)
NRSYS(mutex_create)
NRSYS(mutex_delete)
NRSYS(mutex_take)
NRSYS(mutex_release)   /* 25 */
NRSYS(event_create)
NRSYS(event_delete)
NRSYS(event_send)
NRSYS(event_recv)
NRSYS(mb_create)       /* 30 */
NRSYS(mb_delete)
NRSYS(mb_send)
NRSYS(mb_send_wait)
NRSYS(mb_recv)
NRSYS(mq_create)       /* 35 */
NRSYS(mq_delete)
NRSYS(mq_send)
NRSYS(mq_urgent)
NRSYS(mq_recv)
NRSYS(thread_create)   /* 40 */
NRSYS(thread_delete)
NRSYS(thread_startup)
NRSYS(thread_self)
NRSYS(channel_open)
NRSYS(channel_close)   /* 45 */
NRSYS(channel_send)
NRSYS(channel_send_recv_timeout)
NRSYS(channel_reply)
NRSYS(channel_recv_timeout)
NRSYS(enter_critical)  /* 50 */
NRSYS(exit_critical)

NRSYS(brk)
NRSYS(mmap2)
NRSYS(munmap)

NRSYS(shmget)        /* 55 */
NRSYS(shmrm)
NRSYS(shmat)
NRSYS(shmdt)

/* The following syscalls requre to be arranged. */
NRSYS(rt_device_init)
NRSYS(rt_device_register) /* 60 */
NRSYS(rt_device_control)
NRSYS(rt_device_find)
NRSYS(rt_device_open)
NRSYS(rt_device_close)
NRSYS(rt_device_read) /* 65 */
NRSYS(rt_device_write)

NRSYS(stat)
NRSYS(rt_thread_find)

NRSYS(accept)
NRSYS(bind)            /* 70 */
NRSYS(shutdown)
NRSYS(getpeername)
NRSYS(getsockname)
NRSYS(getsockopt)
NRSYS(setsockopt)      /* 75 */
NRSYS(connect)
NRSYS(listen)
NRSYS(recv)
NRSYS(recvfrom)
NRSYS(send)             /* 80 */
NRSYS(sendto)
NRSYS(socket)

NRSYS(closesocket)
NRSYS(getaddrinfo)
NRSYS(gethostbyname2_r) /* 85 */
NRSYS(sendmsg)
NRSYS(recvmsg)
NRSYS(network_resv2)
NRSYS(network_resv3)
NRSYS(network_resv4)    /* 90 */
NRSYS(network_resv5)
NRSYS(network_resv6)
NRSYS(network_resv7)

NRSYS(select)

NRSYS(_lock)             /* 95 */
NRSYS(_unlock)

NRSYS(rt_tick_get)
NRSYS(exit_group)

NRSYS(rt_delayed_work_init)
NRSYS(rt_work_submit)    /* 100 */
NRSYS(rt_wqueue_wakeup)
NRSYS(rt_thread_mdelay)
NRSYS(sigaction)
NRSYS(sigprocmask)
NRSYS(tkill)             /* 105 */
NRSYS(thread_sigprocmask)
NRSYS(cacheflush)
NRSYS(rt_setaffinity)
NRSYS(rt_thread_resv1)
NRSYS(waitpid)           /* 110 */
NRSYS(rt_timer_create)
NRSYS(rt_timer_delete)
NRSYS(rt_timer_settime)
NRSYS(rt_timer_gettime)
NRSYS(rt_timer_getoverrun) /* 115 */

NRSYS(getcwd)
NRSYS(chdir)
NRSYS(unlink)
NRSYS(mkdir)
NRSYS(rmdir)              /* 120 */
NRSYS(getdents)

NRSYS(rt_get_errno)
NRSYS(set_thread_area)
NRSYS(set_tid_address)
NRSYS(access)             /* 125 */
NRSYS(pipe)
NRSYS(clock_settime)
NRSYS(clock_gettime)
NRSYS(clock_getres)
NRSYS(clone)              /* 130 */
NRSYS(futex)
NRSYS(pmutex)
NRSYS(dup)
NRSYS(dup2)
NRSYS(rename)             /* 135 */
NRSYS(fork)
NRSYS(execve)
NRSYS(vfork)
NRSYS(gettid)
NRSYS(prlimit64)          /* 140 */
NRSYS(getrlimit)
NRSYS(setrlimit)
NRSYS(setsid)

NRSYS(getrandom)
NRSYS(readlink)           /* 145 */
NRSYS(mremap)
NRSYS(madvise)
NRSYS(sched_setparam)
NRSYS(sched_getparam)
NRSYS(sched_get_priority_max)   /* 150 */
NRSYS(sched_get_priority_min)
NRSYS(sched_setscheduler)
NRSYS(sched_getscheduler)
NRSYS(sched_setaffinity)
NRSYS(fsync)                    /* 155 */
NRSYS(clock_nanosleep)
NRSYS(timer_create)
NRSYS(timer_delete)
NRSYS(timer_settime)
NRSYS(timer_gettime)            /* 160 */
NRSYS(timer_getoverrun)

NRSYS(rt_hw_interrupt_disable)
NRSYS(rt_hw_interrupt_enable)

NRSYS(statfs)
NRSYS(sched_yield)              /* 165 */

    _NRSYS_SYSCALL_NR
};
#undef NRSYS

/* clang-format on */

typedef void* rt_device_t;
typedef void* rt_mq_t;

int pthread_get_tid(void);

static inline __attribute__((always_inline)) rt_device_t rt_device_find(const char* name)
{
    return (rt_device_t)syscall(_NRSYS_rt_device_find, (long)name);
}

static inline __attribute__((always_inline)) int rt_device_control(rt_device_t dev, int cmd, void* arg)
{
    return (int)syscall(_NRSYS_rt_device_control, (long)dev, (long)cmd, (long)arg);
}

static inline __attribute__((always_inline)) uint32_t rt_hw_interrupt_disable(void)
{
    return (uint32_t)syscall(_NRSYS_rt_hw_interrupt_disable);
}

static inline __attribute__((always_inline)) void rt_hw_interrupt_enable(uint32_t level)
{
    syscall(_NRSYS_rt_hw_interrupt_enable, (long)level);
}

static inline __attribute__((always_inline)) rt_mq_t rt_mq_create(const char *name, unsigned long msg_size, unsigned long max_msgs, uint8_t flag)
{
    return (rt_mq_t)syscall(_NRSYS_mq_create, name, msg_size, max_msgs, flag);
}

static inline __attribute__((always_inline)) long rt_mq_delete(rt_mq_t mq)
{
    return syscall(_NRSYS_mq_delete, mq);
}

static inline __attribute__((always_inline)) long rt_mq_send(rt_mq_t mq, void *buffer, unsigned long size)
{
    return syscall(_NRSYS_mq_send, mq, buffer, size);
}

static inline __attribute__((always_inline)) long rt_mq_recv(rt_mq_t mq, void *buffer, unsigned long size, unsigned int timeout)
{
    return syscall(_NRSYS_mq_recv, mq, buffer, size, timeout);
}

static inline __attribute__((always_inline)) long rt_mq_urgent(rt_mq_t mq, void *buffer, unsigned long size)
{
    return syscall(_NRSYS_mq_urgent, mq, buffer, size);
}

#ifdef __cplusplus
}
#endif
