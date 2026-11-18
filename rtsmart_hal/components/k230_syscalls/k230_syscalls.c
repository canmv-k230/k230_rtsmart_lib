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
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include "hal_syscall.h"

/* The 64-bit names are macros for the plain ones under _GNU_SOURCE; this
 * file defines both, so that libc.a never has to supply either. */
#undef pread64
#undef pwrite64
#undef preadv64
#undef pwritev64
#undef openat64
#undef fstatat64
#undef lseek64
#undef fallocate64
#undef statfs64
#undef statvfs64
#undef fstatfs64
#undef fstatvfs64

#ifdef posix_fallocate64
#undef posix_fallocate64
#endif

#ifdef ftruncate64
#undef ftruncate64
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
/* syscall() is a bare ecall: it returns the kernel's -errno. */
static long k230_result(long result)
{
    if (result < 0)
    {
        errno = (int)-result;
        return -1;
    }
    return result;
}

int statfs(const char* path, struct statfs* buf)
{
    *buf = (struct statfs) { 0 };
    /* This returned the raw -errno, not -1 with errno set. */
    return (int)k230_result(syscall(_NRSYS_statfs, path, buf));
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

/* The path of an open descriptor, from the kernel's /proc/self/fd/N. */
static int fd_path(int fd, char *out, size_t size)
{
    char link[32];
    ssize_t n;

    snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
    n = readlink(link, out, size - 1);
    if (n < 0)
        return -1;
    if ((size_t)n >= size - 1)
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    out[n] = '\0';
    return 0;
}

/* libc.a's statvfs.o defines all of these, statvfs strongly: one of them
 * pulling it in next to the overrides above was a duplicate definition. */
int fstatfs(int fd, struct statfs *buf)
{
    char path[PATH_MAX];

    if (fd_path(fd, path, sizeof(path)) < 0)
        return -1;
    return statfs(path, buf);
}

int fstatvfs(int fd, struct statvfs *buf)
{
    struct statfs kbuf;

    if (fstatfs(fd, &kbuf) < 0)
        return -1;
    fixup(buf, &kbuf);
    return 0;
}

int statfs64(const char *path, struct statfs *buf) { return statfs(path, buf); }
int statvfs64(const char *restrict path, struct statvfs *restrict buf) { return statvfs(path, buf); }
int fstatfs64(int fd, struct statfs *buf) { return fstatfs(fd, buf); }
int fstatvfs64(int fd, struct statvfs *buf) { return fstatvfs(fd, buf); }

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

int ftruncate(int fd, off_t length)
{
    return (int)k230_result(syscall(_NRSYS_ftruncate, fd, length));
}

int ftruncate64(int fd, off_t length)
{
    return ftruncate(fd, length);
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

///////////////////////////////////////////////////////////////////////////////
// File syscalls the toolchain's musl answers with a fake success /////////////
///////////////////////////////////////////////////////////////////////////////
/*
 * The RT-Thread musl port maps these to stubs that return 0 without calling
 * the kernel ([empty syscall] on the console): pread() looked like EOF,
 * openat() returned fd 0, pipe2() left its array unset, fstatat() left the
 * buffer as it was. Each override below replaces every symbol of the libc.a
 * member it stands for. librtsmart_hal.mk links this object in up front
 * (__k230_syscalls_anchor), so libc's own callers (popen() -> pipe2()) bind
 * to these too. musl's stdio calls readv/writev through its internal syscall
 * table, which no override reaches.
 */
int __k230_syscalls_anchor;

pid_t waitpid(pid_t pid, int *status, int options)
{
    return (pid_t)k230_result(syscall(_NRSYS_waitpid, pid, status, options));
}

pid_t getppid(void) { return (pid_t)k230_result(syscall(_NRSYS_getppid)); }
pid_t getpgid(pid_t pid) { return (pid_t)k230_result(syscall(_NRSYS_getpgid, pid)); }
pid_t getpgrp(void) { return getpgid(0); }
pid_t getsid(pid_t pid) { return (pid_t)k230_result(syscall(_NRSYS_getsid, pid)); }
int setpgid(pid_t pid, pid_t pgid) { return (int)k230_result(syscall(_NRSYS_setpgid, pid, pgid)); }
int setpgrp(void) { return setpgid(0, 0); }

int setitimer(int which, const struct itimerval *restrict value, struct itimerval *restrict old)
{
    return (int)k230_result(syscall(_NRSYS_setitimer, which, value, old));
}

useconds_t ualarm(useconds_t value, useconds_t interval)
{
    struct itimerval v = {
        .it_value = { value / 1000000, value % 1000000 },
        .it_interval = { interval / 1000000, interval % 1000000 }
    }, old;
    if (setitimer(ITIMER_REAL, &v, &old) < 0) return (useconds_t)-1;
    return old.it_value.tv_sec * 1000000 + old.it_value.tv_usec;
}

unsigned alarm(unsigned seconds)
{
    struct itimerval v = { .it_value = { seconds, 0 } }, old;
    if (setitimer(ITIMER_REAL, &v, &old) < 0) return 0;
    return old.it_value.tv_sec + (old.it_value.tv_usec != 0);
}

/* libc's internal readlinkat stub makes realpath treat every component as
 * an empty symbolic link. Normalize DFS paths and check each directory
 * explicitly; descriptor aliases are handled by fd_path above. */
char *realpath(const char *restrict path, char *restrict resolved)
{
    char input[PATH_MAX], output[PATH_MAX] = "/";
    struct stat st;
    size_t used = 1;
    char *part, *save;

    if (!path) { errno = EINVAL; return NULL; }
    if (!*path) { errno = ENOENT; return NULL; }
    if (*path == '/')
    {
        if (strlen(path) >= sizeof(input)) { errno = ENAMETOOLONG; return NULL; }
        strcpy(input, path);
    }
    else
    {
        if (!getcwd(input, sizeof(input))) return NULL;
        if (strlen(input) + strlen(path) + 2 > sizeof(input)) { errno = ENAMETOOLONG; return NULL; }
        strcat(input, "/"); strcat(input, path);
    }
    for (part = strtok_r(input, "/", &save); part; part = strtok_r(NULL, "/", &save))
    {
        if (stat(output, &st) < 0) return NULL;
        if (!S_ISDIR(st.st_mode)) { errno = ENOTDIR; return NULL; }
        if (!strcmp(part, ".")) continue;
        if (!strcmp(part, ".."))
        {
            while (used > 1 && output[--used] != '/') ;
            output[used] = 0;
            continue;
        }
        if (used > 1) output[used++] = '/';
        if (used + strlen(part) >= sizeof(output)) { errno = ENAMETOOLONG; return NULL; }
        strcpy(output + used, part); used += strlen(part);
    }
    if (stat(output, &st) < 0) return NULL;
    if (path[strlen(path) - 1] == '/' && !S_ISDIR(st.st_mode))
    { errno = ENOTDIR; return NULL; }
    if (!resolved && !(resolved = malloc(used + 1))) return NULL;
    memcpy(resolved, output, used + 1);
    return resolved;
}

/* fcntl() and ioctl() share one system call here; a device driver's ioctl
 * commands 0, 1, 2 (ADC, I2C slave, ...) are F_DUPFD, F_GETFD, F_SETFD.
 * Tagged, the kernel knows these are fcntl (DFS_FCNTL_TAG in the kernel's
 * dfs.h: user-space ABI, so the two must ship together).
 * F_GETOWN/F_SETOWN have no kernel ownership support; they currently fall
 * through to the backend ioctl and must not be used for SIGIO ownership. */
#define DFS_FCNTL_TAG 0x46430000

int fcntl(int fd, int cmd, ...)
{
    unsigned long arg;
    va_list ap;

    arg = 0;
    if (cmd != F_GETFD && cmd != F_GETFL && cmd != F_GETOWN)
    {
        va_start(ap, cmd);
        arg = va_arg(ap, unsigned long);
        va_end(ap);
    }
    if (cmd == F_SETFL)
        arg |= O_LARGEFILE;     /* as musl's fcntl() */
    return (int)k230_result(syscall(_NRSYS_ioctl, fd, DFS_FCNTL_TAG | cmd, arg));
}

/* musl's lseek() goes through _llseek, whose wrapper turns every error into
 * EPERM. */
off_t lseek(int fd, off_t offset, int whence)
{
    return (off_t)k230_result(syscall(_NRSYS_lseek, fd, offset, whence));
}

off_t lseek64(int fd, off_t offset, int whence) { return lseek(fd, offset, whence); }

/* No positional I/O in the kernel: seek, transfer, seek back. Not atomic
 * against other users of the same open file. ESPIPE on pipes and sockets. */
static ssize_t positional_io(int fd, void *buf, size_t count, off_t offset, int write_it)
{
    off_t saved;
    ssize_t n;
    int err;

    if (offset < 0)
    {
        errno = EINVAL;
        return -1;
    }
    if (!write_it)
    {
        struct stat st;

        /* Past the end reads nothing; seeking there first would grow a FAT
         * file opened for writing. */
        if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && offset >= st.st_size)
            return 0;
    }
    saved = lseek(fd, 0, SEEK_CUR);
    if (saved < 0 || lseek(fd, offset, SEEK_SET) < 0)
        return -1;
    n = write_it ? write(fd, buf, count) : read(fd, buf, count);
    err = errno;
    lseek(fd, saved, SEEK_SET);
    errno = err;
    return n;
}

ssize_t pread(int fd, void *buf, size_t count, off_t offset)
{
    return positional_io(fd, buf, count, offset, 0);
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    return positional_io(fd, (void *)buf, count, offset, 1);
}

ssize_t pread64(int fd, void *buf, size_t count, off_t offset) { return pread(fd, buf, count, offset); }
ssize_t pwrite64(int fd, const void *buf, size_t count, off_t offset) { return pwrite(fd, buf, count, offset); }

/* Stops at the first short transfer, as readv()/writev() must: musl's
 * emulation read on into the next buffer (and could block there). */
static ssize_t vector_io(int fd, const struct iovec *iov, int iovcnt, off_t offset, int write_it)
{
    ssize_t total = 0;
    int i;

    if (iovcnt < 0 || iovcnt > IOV_MAX)
    {
        errno = EINVAL;
        return -1;
    }
    for (i = 0; i < iovcnt; i++)
    {
        ssize_t n;

        if (iov[i].iov_len == 0)
            continue;
        if (offset >= 0)
            n = positional_io(fd, iov[i].iov_base, iov[i].iov_len, offset + total, write_it);
        else if (write_it)
            n = write(fd, iov[i].iov_base, iov[i].iov_len);
        else
            n = read(fd, iov[i].iov_base, iov[i].iov_len);
        if (n < 0)
            return total ? total : -1;
        total += n;
        if ((size_t)n < iov[i].iov_len)
            break;
    }
    return total;
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt) { return vector_io(fd, iov, iovcnt, -1, 0); }
ssize_t writev(int fd, const struct iovec *iov, int iovcnt) { return vector_io(fd, iov, iovcnt, -1, 1); }

ssize_t preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
    if (offset < 0)
    {
        errno = EINVAL;
        return -1;
    }
    return vector_io(fd, iov, iovcnt, offset, 0);
}

ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
    if (offset < 0)
    {
        errno = EINVAL;
        return -1;
    }
    return vector_io(fd, iov, iovcnt, offset, 1);
}

ssize_t preadv64(int fd, const struct iovec *iov, int iovcnt, off_t offset) { return preadv(fd, iov, iovcnt, offset); }
ssize_t pwritev64(int fd, const struct iovec *iov, int iovcnt, off_t offset) { return pwritev(fd, iov, iovcnt, offset); }

/* Flags applied after the fact: not atomic against a concurrent fork(). */
static int set_fd_flags(int fd, int flags)
{
    if ((flags & O_CLOEXEC) && fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
        return -1;
    if ((flags & O_NONBLOCK) && fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) < 0)
        return -1;
    return 0;
}

/* No native pipe2: pipe() publishes both fds before flags are set. A
 * concurrent fork()/execve() can inherit them before FD_CLOEXEC is set. */
int pipe2(int fds[2], int flags)
{
    int p[2];

    if (flags & ~(O_CLOEXEC | O_NONBLOCK))
    {
        errno = EINVAL;
        return -1;
    }
    if (pipe(p) < 0)
        return -1;
    if (set_fd_flags(p[0], flags) < 0 || set_fd_flags(p[1], flags) < 0)
    {
        int err = errno;

        close(p[0]);
        close(p[1]);
        errno = err;
        return -1;
    }
    fds[0] = p[0];
    fds[1] = p[1];
    return 0;
}

/* No native dup3: dup2() publishes newfd before F_SETFD. A concurrent
 * fork()/execve() can inherit it before FD_CLOEXEC is set. */
int __dup3(int oldfd, int newfd, int flags)
{
    if (oldfd == newfd || (flags & ~O_CLOEXEC))
    {
        errno = EINVAL;
        return -1;
    }
    if (dup2(oldfd, newfd) < 0)
        return -1;
    if ((flags & O_CLOEXEC) && fcntl(newfd, F_SETFD, FD_CLOEXEC) < 0)
        return -1;
    return newfd;
}

int dup3(int oldfd, int newfd, int flags) { return __dup3(oldfd, newfd, flags); }

int fdatasync(int fd)
{
    return fsync(fd);
}

int fallocate(int fd, int mode, off_t offset, off_t len)
{
    int err;

    if (mode != 0)
    {
        errno = EOPNOTSUPP;
        return -1;
    }
    err = posix_fallocate(fd, offset, len);
    if (err)
    {
        errno = err;
        return -1;
    }
    return 0;
}

int fallocate64(int fd, int mode, off_t offset, off_t len) { return fallocate(fd, mode, offset, len); }

/* *at(): a path relative to @dirfd, through the directory's own path (the
 * kernel has no *at calls). */
static int at_path(int dirfd, const char *path, char *out, size_t size)
{
    size_t dlen, plen;
    struct stat st;

    if (!path)
    {
        errno = EFAULT;
        return -1;
    }
    plen = strlen(path);
    if (path[0] == '/' || dirfd == AT_FDCWD)
    {
        if (plen >= size)
        {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(out, path, plen + 1);
        return 0;
    }
    if (plen == 0)
    {
        errno = ENOENT;
        return -1;
    }
    if (fd_path(dirfd, out, size) < 0)
        return -1;
    if (stat(out, &st) < 0)
        return -1;
    if (!S_ISDIR(st.st_mode))
    {
        errno = ENOTDIR;
        return -1;
    }
    dlen = strlen(out);
    if (dlen + 1 + plen >= size)
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (dlen == 0 || out[dlen - 1] != '/')
        out[dlen++] = '/';
    memcpy(out + dlen, path, plen + 1);
    return 0;
}

int openat(int dirfd, const char *path, int flags, ...)
{
    char full[PATH_MAX];
    mode_t mode = 0;

    if (flags & (O_CREAT | O_TMPFILE))
    {
        va_list ap;

        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    if (at_path(dirfd, path, full, sizeof(full)) < 0)
        return -1;
    return open(full, flags, mode);
}

int openat64(int dirfd, const char *path, int flags, ...)
{
    mode_t mode = 0;

    if (flags & (O_CREAT | O_TMPFILE))
    {
        va_list ap;

        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    return openat(dirfd, path, flags, mode);
}

int fstatat(int dirfd, const char *restrict path, struct stat *restrict st, int flag)
{
    char full[PATH_MAX];

    if ((flag & AT_EMPTY_PATH) && path && path[0] == '\0')
        return fstat(dirfd, st);
    /* AT_SYMLINK_NOFOLLOW: no filesystem here has symbolic links. */
    if (at_path(dirfd, path, full, sizeof(full)) < 0)
        return -1;
    return stat(full, st);
}

int fstatat64(int dirfd, const char *restrict path, struct stat *restrict st, int flag)
{
    return fstatat(dirfd, path, st, flag);
}

int faccessat(int dirfd, const char *path, int amode, int flag)
{
    char full[PATH_MAX];

    (void)flag;
    if (at_path(dirfd, path, full, sizeof(full)) < 0)
        return -1;
    return access(full, amode);
}

int mkdirat(int dirfd, const char *path, mode_t mode)
{
    char full[PATH_MAX];

    if (at_path(dirfd, path, full, sizeof(full)) < 0)
        return -1;
    return mkdir(full, mode);
}

int unlinkat(int dirfd, const char *path, int flag)
{
    char full[PATH_MAX];

    if (flag & ~AT_REMOVEDIR)
    {
        errno = EINVAL;
        return -1;
    }
    if (at_path(dirfd, path, full, sizeof(full)) < 0)
        return -1;
    return (flag & AT_REMOVEDIR) ? rmdir(full) : unlink(full);
}

int renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath)
{
    char from[PATH_MAX], to[PATH_MAX];

    if (at_path(olddirfd, oldpath, from, sizeof(from)) < 0 ||
        at_path(newdirfd, newpath, to, sizeof(to)) < 0)
        return -1;
    return rename(from, to);
}

ssize_t readlinkat(int dirfd, const char *restrict path, char *restrict buf, size_t size)
{
    char full[PATH_MAX];

    if (at_path(dirfd, path, full, sizeof(full)) < 0)
        return -1;
    return readlink(full, buf, size);
}
