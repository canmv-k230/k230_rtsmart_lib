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

#define _GNU_SOURCE

#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <time.h>

#include "hal_syscall.h"

#ifdef posix_fallocate64
#undef posix_fallocate64
#endif

#define RT_FIOPREALLOCATE 0x52540003U

#define RTSMART_ABI_ASSERT(name, condition) \
    typedef char rtsmart_abi_assert_##name[(condition) ? 1 : -1]

RTSMART_ABI_ASSERT(socklen_size, sizeof(socklen_t) == 4);
RTSMART_ABI_ASSERT(iovec_size, sizeof(struct iovec) == 16);
RTSMART_ABI_ASSERT(msghdr_size, sizeof(struct msghdr) == 56);
RTSMART_ABI_ASSERT(msghdr_name_offset, offsetof(struct msghdr, msg_name) == 0);
RTSMART_ABI_ASSERT(msghdr_namelen_offset, offsetof(struct msghdr, msg_namelen) == 8);
RTSMART_ABI_ASSERT(msghdr_iov_offset, offsetof(struct msghdr, msg_iov) == 16);
RTSMART_ABI_ASSERT(msghdr_iovlen_offset, offsetof(struct msghdr, msg_iovlen) == 24);
RTSMART_ABI_ASSERT(msghdr_control_offset, offsetof(struct msghdr, msg_control) == 32);
RTSMART_ABI_ASSERT(msghdr_controllen_offset, offsetof(struct msghdr, msg_controllen) == 40);
RTSMART_ABI_ASSERT(msghdr_flags_offset, offsetof(struct msghdr, msg_flags) == 48);
RTSMART_ABI_ASSERT(cmsghdr_size, sizeof(struct cmsghdr) == 16);
RTSMART_ABI_ASSERT(mmsghdr_size, sizeof(struct mmsghdr) == 64);
RTSMART_ABI_ASSERT(mmsghdr_len_offset, offsetof(struct mmsghdr, msg_len) == 56);
RTSMART_ABI_ASSERT(timespec_size, sizeof(struct timespec) == 16);
RTSMART_ABI_ASSERT(sendmsg_syscall_number, _NRSYS_sendmsg == 86);
RTSMART_ABI_ASSERT(recvmsg_syscall_number, _NRSYS_recvmsg == 87);

struct dfs_preallocate_args
{
    off_t offset;
    off_t len;
};

///////////////////////////////////////////////////////////////////////////////
// Syscalls for statfs and statvfs ////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
int statfs(const char* path, struct statfs* buf)
{
    *buf = (struct statfs) { 0 };
    return syscall(_NRSYS_statfs, path, buf);
}

static void fixup(struct statvfs* out, const struct statfs* in)
{
    *out = (struct statvfs) { 0 };

    out->f_bsize   = in->f_bsize;
    out->f_frsize  = in->f_frsize ? in->f_frsize : in->f_bsize;
    out->f_blocks  = in->f_blocks;
    out->f_bfree   = in->f_bfree;
    out->f_bavail  = in->f_bavail;
    out->f_files   = in->f_files;
    out->f_ffree   = in->f_ffree;
    out->f_favail  = in->f_ffree;
    out->f_fsid    = in->f_fsid.__val[0];
    out->f_flag    = in->f_flags;
    out->f_namemax = in->f_namelen;
}

int statvfs(const char* restrict path, struct statvfs* restrict buf)
{
    struct statfs kbuf;
    if (statfs(path, &kbuf) < 0)
        return -1;
    fixup(buf, &kbuf);
    return 0;
}

/*
 * FatFs f_expand() only supports allocating an empty file from offset 0 and
 * does not provide general sparse/zero-fill semantics for arbitrary ranges.
 */
int posix_fallocate(int fd, off_t offset, off_t len)
{
    struct dfs_preallocate_args args;

    if (offset < 0 || len <= 0)
        return EINVAL;

    args.offset = offset;
    args.len = len;

    if (ioctl(fd, RT_FIOPREALLOCATE, &args) == 0)
        return 0;

    return errno ? errno : EIO;
}

int posix_fallocate64(int fd, off_t offset, off_t len)
{
    return posix_fallocate(fd, offset, len);
}

#define PMUTEX_LOCK    1
#define PMUTEX_DESTROY 3

static int pmutex_result(long ret)
{
    return ret < 0 ? (int)-ret : (int)ret;
}

int pthread_mutex_lock(pthread_mutex_t *m)
{
    long ret;

    if (m == NULL)
        return EINVAL;

    for (;;)
    {
        ret = syscall(_NRSYS_pmutex, (long)m, PMUTEX_LOCK, 0);
        /* pthread mutex locking must not expose EINTR to its caller. */
        if (pmutex_result(ret) != EINTR)
            break;
    }

    return pmutex_result(ret);
}

int pthread_get_tid(void)
{
    return syscall(_NRSYS_gettid);
}

static long socket_syscall_result(long result)
{
    if (result < 0)
    {
        errno = (int)-result;
        return -1;
    }
    return result;
}

ssize_t sendmsg(int socket, const struct msghdr *message, int flags)
{
    return (ssize_t)socket_syscall_result(
            syscall(_NRSYS_sendmsg, socket, message, flags));
}

ssize_t recvmsg(int socket, struct msghdr *message, int flags)
{
    return (ssize_t)socket_syscall_result(
            syscall(_NRSYS_recvmsg, socket, message, flags));
}

int shutdown(int socket, int how)
{
    return (int)socket_syscall_result(
            syscall(_NRSYS_shutdown, socket, how));
}

int accept4(int socket, struct sockaddr *restrict address,
        socklen_t *restrict address_len, int flags)
{
    int accepted;

    if (flags & ~(SOCK_NONBLOCK | SOCK_CLOEXEC))
    {
        errno = EINVAL;
        return -1;
    }
    if (flags & SOCK_CLOEXEC)
    {
        errno = EOPNOTSUPP;
        return -1;
    }

    /* SOCK_NONBLOCK applies to the accepted socket, not to this accept
     * operation. Keep a blocking listener blocking while waiting for a peer. */
    accepted = accept(socket, address, address_len);
    if (accepted >= 0 && (flags & SOCK_NONBLOCK))
    {
        int status_flags = fcntl(accepted, F_GETFL);

        if (status_flags < 0 ||
                fcntl(accepted, F_SETFL, status_flags | O_NONBLOCK) < 0)
        {
            int saved_errno = errno;
            close(accepted);
            errno = saved_errno;
            return -1;
        }
    }
    return accepted;
}

int sendmmsg(int socket, struct mmsghdr *messages, unsigned int vlen,
        unsigned int flags)
{
    unsigned int i;

    if (!vlen)
    {
        return 0;
    }
    if (!messages)
    {
        errno = EFAULT;
        return -1;
    }
    if (vlen > IOV_MAX)
    {
        vlen = IOV_MAX;
    }

    for (i = 0; i < vlen; i++)
    {
        ssize_t result = sendmsg(socket, &messages[i].msg_hdr, (int)flags);
        if (result < 0)
        {
            return i ? (int)i : -1;
        }
        messages[i].msg_len = (unsigned int)result;
    }
    return (int)i;
}

static int timespec_to_timeout_ms(const struct timespec *timeout)
{
    long long milliseconds;

    if (!timeout)
    {
        return -1;
    }
    if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000L)
    {
        errno = EINVAL;
        return -2;
    }
    if (timeout->tv_sec > 0x7fffffff / 1000)
    {
        return 0x7fffffff;
    }
    milliseconds = (long long)timeout->tv_sec * 1000 +
            (timeout->tv_nsec + 999999) / 1000000;
    return milliseconds > 0x7fffffff ? 0x7fffffff : (int)milliseconds;
}

static void timeout_ms_to_timespec(struct timespec *timeout, int timeout_ms)
{
    timeout->tv_sec = timeout_ms / 1000;
    timeout->tv_nsec = (long)(timeout_ms % 1000) * 1000000L;
}

int recvmmsg(int socket, struct mmsghdr *messages, unsigned int vlen,
        unsigned int flags, struct timespec *timeout)
{
    struct timespec started;
    unsigned int i;
    int timeout_ms;

    if (!vlen || vlen > IOV_MAX)
    {
        errno = EINVAL;
        return -1;
    }
    if (!messages)
    {
        errno = EFAULT;
        return -1;
    }

    timeout_ms = timespec_to_timeout_ms(timeout);
    if (timeout_ms == -2)
    {
        return -1;
    }
    if (timeout && clock_gettime(CLOCK_MONOTONIC, &started) < 0)
    {
        return -1;
    }

    for (i = 0; i < vlen; i++)
    {
        int receive_flags = (int)(flags & ~MSG_WAITFORONE);
        ssize_t result;

        if (i && (flags & MSG_WAITFORONE))
        {
            receive_flags |= MSG_DONTWAIT;
        }
        if (timeout && !(receive_flags & MSG_DONTWAIT))
        {
            struct pollfd pfd = { socket, POLLIN, 0 };
            int ready = poll(&pfd, 1, timeout_ms);

            if (ready <= 0)
            {
                if (ready == 0)
                {
                    timeout_ms_to_timespec(timeout, 0);
                    return (int)i;
                }
                return i ? (int)i : -1;
            }
        }

        result = recvmsg(socket, &messages[i].msg_hdr, receive_flags);
        if (result < 0)
        {
            return i ? (int)i : -1;
        }
        messages[i].msg_len = (unsigned int)result;

        if (timeout)
        {
            struct timespec now;
            long long elapsed_ms;

            if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
            {
                return (int)(i + 1);
            }
            elapsed_ms = (long long)(now.tv_sec - started.tv_sec) * 1000 +
                    (now.tv_nsec - started.tv_nsec) / 1000000;
            timeout_ms = elapsed_ms >= timeout_ms ? 0 : timeout_ms - (int)elapsed_ms;
            started = now;
            timeout_ms_to_timespec(timeout, timeout_ms);
        }
    }
    return (int)i;
}

int socketpair(int domain, int type, int protocol, int sockets[2])
{
    (void)domain;
    (void)type;
    (void)protocol;
    (void)sockets;
    errno = EAFNOSUPPORT;
    return -1;
}

int sched_yield(void)
{
    return (int)syscall(_NRSYS_sched_yield);
}
