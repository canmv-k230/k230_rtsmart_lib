/* PUF Secure Engine SHA-512 ALT implementation for MbedTLS
 * Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "mbedtls/sha512.h"

#if defined(MBEDTLS_SHA512_ALT)

#include <string.h>
#include <pthread.h>
#include "drv_pufs.h"
#include "hal_rvv_ops.h"

static drv_pufs_inst s_sha512_dev;
static int s_sha512_dev_ready = 0;
static pthread_once_t s_sha512_once = PTHREAD_ONCE_INIT;

static void sha512_dev_init_once(void)
{
    if (drv_pufs_open(&s_sha512_dev) == 0)
        s_sha512_dev_ready = 1;
}

static drv_pufs_inst *sha512_get_dev(void)
{
    pthread_once(&s_sha512_once, sha512_dev_init_once);
    return s_sha512_dev_ready ? &s_sha512_dev : NULL;
}

void mbedtls_sha512_init(mbedtls_sha512_context *ctx)
{
    if (ctx)
        hal_rvv_memset(ctx, 0, sizeof(*ctx));
}

void mbedtls_sha512_free(mbedtls_sha512_context *ctx)
{
    if (!ctx)
        return;

    hal_rvv_memset(ctx, 0, sizeof(*ctx));
}

void mbedtls_sha512_clone(mbedtls_sha512_context *dst,
                          const mbedtls_sha512_context *src)
{
    if (!dst || !src)
        return;

    hal_rvv_memcpy(dst, src, sizeof(*dst));

    if (dst->started) {
        drv_pufs_inst *dev = sha512_get_dev();
        if (!dev)
            dst->started = 0;
        else
            dst->hash.dev = dev;
    }
}

int mbedtls_sha512_starts(mbedtls_sha512_context *ctx, int is384)
{
    drv_pufs_inst *dev;
    pufs_hashtype_t hashtype;
    int ret;

    if (!ctx)
        return MBEDTLS_ERR_SHA512_BAD_INPUT_DATA;

    if (is384 != 0 && is384 != 1)
        return MBEDTLS_ERR_SHA512_BAD_INPUT_DATA;

    dev = sha512_get_dev();
    if (!dev)
        return MBEDTLS_ERR_SHA512_BAD_INPUT_DATA;

    ctx->is384 = is384;
    ctx->started = 0;

    hashtype = is384 ? HASH_SHA_384 : HASH_SHA_512;
    ret = drv_pufs_hash_init(&ctx->hash, dev, hashtype);
    if (ret != 0)
        return MBEDTLS_ERR_SHA512_BAD_INPUT_DATA;

    ctx->started = 1;

    return 0;
}

int mbedtls_sha512_update(mbedtls_sha512_context *ctx,
                          const unsigned char *input,
                          size_t ilen)
{
    if (!ctx || !ctx->started)
        return MBEDTLS_ERR_SHA512_BAD_INPUT_DATA;

    if (ilen == 0)
        return 0;

    if (!input)
        return MBEDTLS_ERR_SHA512_BAD_INPUT_DATA;

    const uint8_t *p = input;
    size_t remaining = ilen;

    while (remaining > 0) {
        uint32_t chunk = (remaining > 0x7FFFFFFF) ? 0x7FFFFFFF : (uint32_t)remaining;
        int ret = drv_pufs_hash_update(&ctx->hash, p, chunk);
        if (ret != 0)
            return MBEDTLS_ERR_SHA512_BAD_INPUT_DATA;
        p += chunk;
        remaining -= chunk;
    }

    return 0;
}

int mbedtls_sha512_finish(mbedtls_sha512_context *ctx,
                          unsigned char *output)
{
    int ret;
    if (!ctx || !ctx->started || !output)
        return MBEDTLS_ERR_SHA512_BAD_INPUT_DATA;

    uint32_t dlen = 0;
    ret = drv_pufs_hash_final(&ctx->hash, output, &dlen);
    ctx->started = 0;
    if (ret != 0)
        return MBEDTLS_ERR_SHA512_BAD_INPUT_DATA;

    return 0;
}

#endif /* MBEDTLS_SHA512_ALT */
