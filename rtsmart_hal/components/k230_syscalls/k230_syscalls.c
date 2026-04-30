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

#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <pthread.h>

#include "hal_syscall.h"

#define RT_FIOPREALLOCATE 0x52540003U

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

/*
#define PMUTEX_INIT    0
#define PMUTEX_LOCK    1
#define PMUTEX_UNLOCK  2
#define PMUTEX_DESTROY 3
*/

int pthread_mutex_lock(pthread_mutex_t *m)
{
    int retry = 0;

    while(0x00 != syscall(_NRSYS_pmutex, (long)m, 1, 0)) {
        retry++;
        if (retry > 100) {
            retry = 0;
            printf("pthread_mutex_lock: failed to lock mutex after %d tries\n", retry);
        }

        usleep(1000); // sleep for 1ms before retrying to avoid busy-waiting
    }

    return 0; // success
}

int pthread_get_tid(void)
{
    return syscall(_NRSYS_gettid);
}
