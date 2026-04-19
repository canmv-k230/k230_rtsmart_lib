/* PUF Secure Engine GCM ALT implementation for MbedTLS
 * Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "mbedtls/gcm.h"

#if defined(MBEDTLS_GCM_ALT)

#include <string.h>
#include <pthread.h>
#include "mbedtls/error.h"
#include "drv_pufs.h"
#include "hal_rvv_ops.h"

/* Shared device handle */
static drv_pufs_inst s_gcm_dev;
static int s_gcm_dev_ready = 0;
static pthread_once_t s_gcm_once = PTHREAD_ONCE_INIT;

static void gcm_dev_init_once(void)
{
    if (drv_pufs_open(&s_gcm_dev) == 0)
        s_gcm_dev_ready = 1;
}

static drv_pufs_inst *gcm_get_dev(void)
{
    pthread_once(&s_gcm_once, gcm_dev_init_once);
    return s_gcm_dev_ready ? &s_gcm_dev : NULL;
}

static int gcm_map_cipher_error(mbedtls_gcm_context *ctx, int ret)
{
    if (ret == 0)
        return 0;

    if (ret == PUFS_ERR_VERFAIL && ctx && ctx->mode == MBEDTLS_GCM_DECRYPT)
        return MBEDTLS_ERR_GCM_AUTH_FAILED;

    return MBEDTLS_ERR_GCM_BAD_INPUT;
}

static int gcm_run_stream(mbedtls_gcm_context *ctx,
                          int mode,
                          const unsigned char *iv,
                          size_t iv_len,
                          const unsigned char *add,
                          size_t add_len,
                          const unsigned char *input,
                          size_t length,
                          unsigned char *output,
                          unsigned char *tag,
                          size_t tag_len)
{
    drv_pufs_inst *dev;
    int ret;

    dev = gcm_get_dev();
    if (!dev)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    ctx->mode = mode;

    /*
     * Fast path: 2 ioctls using compound ops (INIT_UPDATE + UPDATE_FINAL).
     * Saves 2 syscall round-trips vs the 4-ioctl streaming path.
     */
    {
        uint32_t outlen = 0;

        ret = drv_pufs_cipher_init_update(&ctx->cipher, dev, SK_AES, MODE_GCM,
                                          mode == MBEDTLS_GCM_ENCRYPT,
                                          KT_SWKEY, ctx->key, ctx->keybits,
                                          iv, (uint32_t)iv_len,
                                          NULL, NULL,
                                          add, (uint32_t)add_len);
        if (ret != 0)
            goto fallback;

        ctx->started = 1;

        ret = drv_pufs_cipher_update_final(&ctx->cipher,
                                           output, &outlen,
                                           input, (uint32_t)length,
                                           tag, (uint32_t)tag_len);
        ctx->started = 0;

        if (ret != 0)
            return gcm_map_cipher_error(ctx, ret);

        if (outlen != (uint32_t)length)
            return MBEDTLS_ERR_GCM_BAD_INPUT;

        return 0;
    }

fallback:
    /* Streaming fallback (4 ioctls) for unsupported compound ops */
    {
        size_t update_length = 0;
        size_t finish_length = 0;
        size_t total_output_length = 0;

        ret = mbedtls_gcm_starts(ctx, mode, iv, iv_len);
        if (ret != 0)
            return ret;

        ret = mbedtls_gcm_update_ad(ctx, add, add_len);
        if (ret != 0)
            goto cleanup;

        ret = mbedtls_gcm_update(ctx, input, length, output, length, &update_length);
        if (ret != 0)
            goto cleanup;

        if (update_length > length) {
            ret = MBEDTLS_ERR_GCM_BAD_INPUT;
            goto cleanup;
        }

        total_output_length = update_length;

        ret = mbedtls_gcm_finish(ctx,
                                 output ? output + update_length : NULL,
                                 length - update_length,
                                 &finish_length,
                                 tag,
                                 tag_len);
        if (ret != 0)
            goto cleanup;

        total_output_length += finish_length;
        if (total_output_length != length)
            ret = MBEDTLS_ERR_GCM_BAD_INPUT;

cleanup:
        if (ret != 0)
            ctx->started = 0;

        return ret;
    }
}

void mbedtls_gcm_init(mbedtls_gcm_context *ctx)
{
    if (ctx)
        hal_rvv_memset(ctx, 0, sizeof(*ctx));
}

void mbedtls_gcm_free(mbedtls_gcm_context *ctx)
{
    if (ctx)
        hal_rvv_memset(ctx, 0, sizeof(*ctx));
}

int mbedtls_gcm_setkey(mbedtls_gcm_context *ctx,
                       mbedtls_cipher_id_t cipher,
                       const unsigned char *key,
                       unsigned int keybits)
{
    if (!ctx || !key)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    if (cipher != MBEDTLS_CIPHER_ID_AES)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    if (keybits != 128 && keybits != 192 && keybits != 256)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    hal_rvv_memcpy(ctx->key, key, keybits / 8);
    ctx->keybits = keybits;
    ctx->has_key = 1;
    ctx->started = 0;
    return 0;
}

int mbedtls_gcm_starts(mbedtls_gcm_context *ctx,
                       int mode,
                       const unsigned char *iv,
                       size_t iv_len)
{
    drv_pufs_inst *dev;
    int ret;

    if (!ctx || !ctx->has_key || !iv)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    if (mode != MBEDTLS_GCM_ENCRYPT && mode != MBEDTLS_GCM_DECRYPT)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    dev = gcm_get_dev();
    if (!dev)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    ctx->mode = mode;

    ret = drv_pufs_cipher_init(&ctx->cipher, dev, SK_AES, MODE_GCM,
                               mode == MBEDTLS_GCM_ENCRYPT,
                               KT_SWKEY, ctx->key, ctx->keybits,
                               iv, (uint32_t)iv_len);
    if (ret != 0)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    ctx->started = 1;
    return 0;
}

int mbedtls_gcm_update_ad(mbedtls_gcm_context *ctx,
                          const unsigned char *add,
                          size_t add_len)
{
    uint32_t outlen = 0;

    if (!ctx || !ctx->started)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    if (add_len == 0)
        return 0;

    if (!add)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    /* Pass AAD with NULL output — firmware detects AAD mode this way */
    int ret = drv_pufs_cipher_update(&ctx->cipher,
                                     NULL, &outlen,
                                     add, (uint32_t)add_len);
    if (ret != 0)
        return gcm_map_cipher_error(ctx, ret);

    return 0;
}

int mbedtls_gcm_update(mbedtls_gcm_context *ctx,
                       const unsigned char *input, size_t input_length,
                       unsigned char *output, size_t output_size,
                       size_t *output_length)
{
    uint32_t outlen = 0;

    if (!ctx || !ctx->started)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    if (output_length)
        *output_length = 0;

    if (input_length == 0)
        return 0;

    if (!input || !output)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    if (output_size < input_length)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    int ret = drv_pufs_cipher_update(&ctx->cipher,
                                     output, &outlen,
                                     input, (uint32_t)input_length);
    if (ret != 0)
        return gcm_map_cipher_error(ctx, ret);

    if (output_length)
        *output_length = outlen;

    return 0;
}

int mbedtls_gcm_finish(mbedtls_gcm_context *ctx,
                       unsigned char *output, size_t output_size,
                       size_t *output_length,
                       unsigned char *tag, size_t tag_len)
{
    uint32_t outlen = 0;
    int ret;

    (void) output_size;

    if (!ctx || !ctx->started)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    if (output_length)
        *output_length = 0;

    ret = drv_pufs_cipher_final(&ctx->cipher,
                                output, &outlen,
                                tag, (uint32_t)tag_len);

    ctx->started = 0;

    if (ret != 0)
        return gcm_map_cipher_error(ctx, ret);

    if (output_length)
        *output_length = outlen;

    return 0;
}

int mbedtls_gcm_crypt_and_tag(mbedtls_gcm_context *ctx,
                              int mode,
                              size_t length,
                              const unsigned char *iv,
                              size_t iv_len,
                              const unsigned char *add,
                              size_t add_len,
                              const unsigned char *input,
                              unsigned char *output,
                              size_t tag_len,
                              unsigned char *tag)
{
    if (!ctx)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    return gcm_run_stream(ctx, mode, iv, iv_len,
                          add, add_len,
                          input, length,
                          output, tag, tag_len);
}

int mbedtls_gcm_auth_decrypt(mbedtls_gcm_context *ctx,
                             size_t length,
                             const unsigned char *iv,
                             size_t iv_len,
                             const unsigned char *add,
                             size_t add_len,
                             const unsigned char *tag,
                             size_t tag_len,
                             const unsigned char *input,
                             unsigned char *output)
{
    unsigned char tag_copy[16];

    if (tag_len > 16 || tag_len < 4)
        return MBEDTLS_ERR_GCM_BAD_INPUT;

    hal_rvv_memcpy(tag_copy, tag, tag_len);

    int ret = gcm_run_stream(ctx, MBEDTLS_GCM_DECRYPT, iv, iv_len,
                             add, add_len,
                             input, length,
                             output, tag_copy, tag_len);
    if (ret != 0) {
        hal_rvv_memset(output, 0, length);
        return ret;
    }

    return 0;
}

#endif /* MBEDTLS_GCM_ALT */
