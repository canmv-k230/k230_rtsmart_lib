/* PUF Secure Engine SHA-256 ALT implementation for MbedTLS
 * Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "mbedtls/sha256.h"

#if defined(MBEDTLS_SHA256_ALT)

#include "drv_pufs.h"
#include "hal_rvv_ops.h"
#include <pthread.h>

static drv_pufs_inst *sha256_get_dev(void);
static int sha256_start_hw(mbedtls_sha256_context *ctx)
{
    drv_pufs_inst *dev;
    pufs_hashtype_t hashtype;
    int ret;

    dev = sha256_get_dev();
    if (!dev)
        return MBEDTLS_ERR_SHA256_BAD_INPUT_DATA;

    hashtype = ctx->is224 ? HASH_SHA_224 : HASH_SHA_256;
    ret = drv_pufs_hash_init(&ctx->hash, dev, hashtype);
    if (ret != 0)
        return MBEDTLS_ERR_SHA256_BAD_INPUT_DATA;

    return 0;
}

static int sha256_hw_update(mbedtls_sha256_context *ctx,
                            const unsigned char *input,
                            size_t ilen)
{
    const uint8_t *cursor = input;
    size_t remaining = ilen;

    while (remaining > 0) {
        uint32_t chunk = (remaining > 0x7FFFFFFF) ? 0x7FFFFFFF : (uint32_t) remaining;
        int ret = drv_pufs_hash_update(&ctx->hash, cursor, chunk);
        if (ret != 0)
            return MBEDTLS_ERR_SHA256_BAD_INPUT_DATA;
        cursor += chunk;
        remaining -= chunk;
    }

    return 0;
}

static int sha256_switch_to_hw(mbedtls_sha256_context *ctx,
                               const unsigned char *input,
                               size_t ilen)
{
    int ret;

    ret = sha256_start_hw(ctx);
    if (ret != 0)
        return ret;

    if (ctx->buffered_len > 0) {
        ret = sha256_hw_update(ctx, ctx->buffered, ctx->buffered_len);
        if (ret != 0)
            return ret;
        /* Also feed buffered data to SW context for clone support */
        ret = mbedtls_sha256_sw_update(&ctx->sw, ctx->buffered, ctx->buffered_len);
        if (ret != 0)
            return ret;
    }

    if (ilen > 0) {
        ret = sha256_hw_update(ctx, input, ilen);
        if (ret != 0)
            return ret;
        /* Keep SW context in sync */
        ret = mbedtls_sha256_sw_update(&ctx->sw, input, ilen);
        if (ret != 0)
            return ret;
    }

    /* Keep SW context alive — needed for clone support */
    ctx->mode = MBEDTLS_PUFS_SHA256_MODE_HW;
    ctx->buffered_len = 0;
    return 0;
}

static drv_pufs_inst s_sha256_dev;
static int s_sha256_dev_ready = 0;
static pthread_once_t s_sha256_once = PTHREAD_ONCE_INIT;

static void sha256_dev_init_once(void)
{
    if (drv_pufs_open(&s_sha256_dev) == 0)
        s_sha256_dev_ready = 1;
}

static drv_pufs_inst *sha256_get_dev(void)
{
    pthread_once(&s_sha256_once, sha256_dev_init_once);
    return s_sha256_dev_ready ? &s_sha256_dev : NULL;
}

void mbedtls_sha256_init(mbedtls_sha256_context *ctx)
{
    if (ctx) {
        hal_rvv_memset(ctx, 0, sizeof(*ctx));
        mbedtls_sha256_sw_init(&ctx->sw);
    }
}

void mbedtls_sha256_free(mbedtls_sha256_context *ctx)
{
    if (!ctx)
        return;

    mbedtls_sha256_sw_free(&ctx->sw);
    hal_rvv_memset(ctx, 0, sizeof(*ctx));
}

void mbedtls_sha256_clone(mbedtls_sha256_context *dst,
                          const mbedtls_sha256_context *src)
{
    if (!dst || !src)
        return;

    hal_rvv_memcpy(dst, src, sizeof(*dst));

    /*
     * The HW hash session cannot be cloned (kernel-side state is shared).
     * The SW context is always kept in sync, so force the clone to use
     * SW-only mode. This is safe: finish will use SW path for the clone
     * while the original can still finish via HW.
     */
    if (dst->started && dst->mode == MBEDTLS_PUFS_SHA256_MODE_HW) {
        dst->mode = MBEDTLS_PUFS_SHA256_MODE_BUFFERED;
        dst->buffered_len = 0;
        /* SW context was copied by memcpy — it holds the correct state */
    }
}

int mbedtls_sha256_starts(mbedtls_sha256_context *ctx, int is224)
{
    int ret;

    if (!ctx)
        return MBEDTLS_ERR_SHA256_BAD_INPUT_DATA;

    if (is224 != 0 && is224 != 1)
        return MBEDTLS_ERR_SHA256_BAD_INPUT_DATA;

    ret = mbedtls_sha256_sw_starts(&ctx->sw, is224);
    if (ret != 0)
        return ret;

    ctx->is224 = is224;
    ctx->mode = MBEDTLS_PUFS_SHA256_MODE_BUFFERED;
    ctx->buffered_len = 0;
    ctx->started = 1;
    return 0;
}

int mbedtls_sha256_update(mbedtls_sha256_context *ctx,
                          const unsigned char *input,
                          size_t ilen)
{
    if (!ctx || !ctx->started)
        return MBEDTLS_ERR_SHA256_BAD_INPUT_DATA;

    if (ilen == 0)
        return 0;

    if (!input)
        return MBEDTLS_ERR_SHA256_BAD_INPUT_DATA;

    if (ctx->mode == MBEDTLS_PUFS_SHA256_MODE_BUFFERED) {
        if (ctx->buffered_len + ilen <= MBEDTLS_PUFS_SHA256_SW_FALLBACK_THRESHOLD) {
            hal_rvv_memcpy(ctx->buffered + ctx->buffered_len, input, ilen);
            ctx->buffered_len += (uint32_t) ilen;
            return 0;
        }

        return sha256_switch_to_hw(ctx, input, ilen);
    }

    if (ctx->mode == MBEDTLS_PUFS_SHA256_MODE_HW) {
        int ret = sha256_hw_update(ctx, input, ilen);
        if (ret != 0)
            return ret;
        /* Keep SW context in sync for clone support */
        return mbedtls_sha256_sw_update(&ctx->sw, input, ilen);
    }

    /* Should not reach here */
    return MBEDTLS_ERR_SHA256_BAD_INPUT_DATA;
}

int mbedtls_sha256_finish(mbedtls_sha256_context *ctx,
                          unsigned char *output)
{
    int ret;

    if (!ctx || !ctx->started || !output)
        return MBEDTLS_ERR_SHA256_BAD_INPUT_DATA;

    if (ctx->mode == MBEDTLS_PUFS_SHA256_MODE_BUFFERED) {
        if (ctx->buffered_len > 0) {
            ret = mbedtls_sha256_sw_update(&ctx->sw, ctx->buffered, ctx->buffered_len);
            if (ret != 0) {
                ctx->started = 0;
                return ret;
            }
        }
        ret = mbedtls_sha256_sw_finish(&ctx->sw, output);
        ctx->started = 0;
        ctx->buffered_len = 0;
        hal_rvv_memset(ctx->buffered, 0, sizeof(ctx->buffered));
        return ret;
    }

    uint32_t dlen = 0;
    ret = drv_pufs_hash_final(&ctx->hash, output, &dlen);
    ctx->started = 0;
    ctx->buffered_len = 0;
    hal_rvv_memset(ctx->buffered, 0, sizeof(ctx->buffered));
    mbedtls_sha256_sw_free(&ctx->sw);
    if (ret != 0)
        return MBEDTLS_ERR_SHA256_BAD_INPUT_DATA;

    return 0;
}

#endif /* MBEDTLS_SHA256_ALT */
