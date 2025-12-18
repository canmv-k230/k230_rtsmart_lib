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
#include "k230_ota.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define OTA_DEV_PATH "/dev/ota"

/* Internal definition: Hidden from the user */
struct k230_ota_ctx {
    int    fd;
    size_t total_written;
};

k230_ota_t* k230_ota_create(void)
{
    k230_ota_t* ctx = malloc(sizeof(struct k230_ota_ctx));
    if (!ctx)
        return NULL;

    ctx->fd = open(OTA_DEV_PATH, O_WRONLY, 0);
    if (ctx->fd < 0) {
        perror("[ota] open /dev/ota failed");
        free(ctx);
        return NULL;
    }

    if (lseek(ctx->fd, 0, SEEK_SET) < 0) {
        perror("[k230_ota] lseek failed");
        close(ctx->fd);
        free(ctx);
        return NULL;
    }

    ctx->total_written = 0;
    printf("[k230_ota] session created\n");
    return ctx;
}

int k230_ota_update(k230_ota_t* ctx, const void* buf, size_t size)
{
    const uint8_t* p = buf;
    ssize_t        n;

    if (!ctx || ctx->fd < 0 || !buf || size == 0)
        return -1;

    size_t remaining = size;
    while (remaining > 0) {
        n = write(ctx->fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("[ota] write failed");
            return -1;
        }
        p += n;
        remaining -= n;
    }

    ctx->total_written += size;
    return 0;
}

void k230_ota_destroy(k230_ota_t* ctx)
{
    if (ctx) {
        if (ctx->fd >= 0) {
            close(ctx->fd);
            printf("[k230_ota] session closed. Total: %zu\n", ctx->total_written);
        }
        free(ctx);
    }
}

int k230_ota_write_file(const char* image_path, size_t chunk_size)
{
    k230_ota_t* ctx    = NULL;
    int         fd_img = -1;
    uint8_t*    buf    = NULL;
    ssize_t     rd;
    int         ret = -1;

    if (!image_path || chunk_size == 0)
        return -1;

    buf = malloc(chunk_size);
    if (!buf)
        goto _exit;

    fd_img = open(image_path, O_RDONLY, 0);
    if (fd_img < 0)
        goto _exit;

    ctx = k230_ota_create();
    if (!ctx)
        goto _exit;

    while ((rd = read(fd_img, buf, chunk_size)) > 0) {
        if (k230_ota_update(ctx, buf, rd) < 0)
            goto _exit;
    }

    if (rd >= 0)
        ret = 0;

_exit:
    if (fd_img >= 0)
        close(fd_img);
    if (ctx)
        k230_ota_destroy(ctx);
    free(buf);
    return ret;
}
