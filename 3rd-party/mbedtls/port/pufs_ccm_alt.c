/* PUF Secure Engine CCM ALT implementation for MbedTLS
 * Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "mbedtls/ccm.h"

#if defined(MBEDTLS_CCM_ALT)

#include <string.h>
#include <pthread.h>
#include "mbedtls/error.h"
#include "drv_pufs.h"
#include "hal_rvv_ops.h"

/* Shared device handle */
static drv_pufs_inst s_ccm_dev;
static int s_ccm_dev_ready = 0;
static pthread_once_t s_ccm_once = PTHREAD_ONCE_INIT;

static void ccm_dev_init_once(void)
{
    if (drv_pufs_open(&s_ccm_dev) == 0)
        s_ccm_dev_ready = 1;
}

static drv_pufs_inst *ccm_get_dev(void)
{
    pthread_once(&s_ccm_once, ccm_dev_init_once);
    return s_ccm_dev_ready ? &s_ccm_dev : NULL;
}

static int ccm_map_cipher_error(mbedtls_ccm_context *ctx, int ret)
{
    if (ret == 0)
        return 0;

    if (ret == PUFS_ERR_VERFAIL && ctx &&
        (ctx->mode == MBEDTLS_CCM_DECRYPT ||
         ctx->mode == MBEDTLS_CCM_STAR_DECRYPT))
        return MBEDTLS_ERR_CCM_AUTH_FAILED;

    return MBEDTLS_ERR_CCM_BAD_INPUT;
}

static int ccm_run_stream(mbedtls_ccm_context *ctx,
                          int mode,
                          size_t length,
                          const unsigned char *iv,
                          size_t iv_len,
                          const unsigned char *ad,
                          size_t ad_len,
                          const unsigned char *input,
                          unsigned char *output,
                          unsigned char *tag,
                          size_t tag_len)
{
    size_t output_len = 0;
    int ret;

    ret = mbedtls_ccm_starts(ctx, mode, iv, iv_len);
    if (ret != 0)
        return ret;

    ret = mbedtls_ccm_set_lengths(ctx, ad_len, length, tag_len);
    if (ret != 0)
        goto cleanup;

    ret = mbedtls_ccm_update_ad(ctx, ad, ad_len);
    if (ret != 0)
        goto cleanup;

    ret = mbedtls_ccm_update(ctx, input, length, output, length, &output_len);
    if (ret != 0)
        goto cleanup;

    if (output_len != length) {
        ret = MBEDTLS_ERR_CCM_BAD_INPUT;
        goto cleanup;
    }

    ret = mbedtls_ccm_finish(ctx, tag, tag_len);

cleanup:
    if (ret != 0)
        ctx->started = 0;

    return ret;
}

void mbedtls_ccm_init(mbedtls_ccm_context *ctx)
{
    if (ctx)
        hal_rvv_memset(ctx, 0, sizeof(*ctx));
}

void mbedtls_ccm_free(mbedtls_ccm_context *ctx)
{
    if (ctx)
        hal_rvv_memset(ctx, 0, sizeof(*ctx));
}

int mbedtls_ccm_setkey(mbedtls_ccm_context *ctx,
                       mbedtls_cipher_id_t cipher,
                       const unsigned char *key,
                       unsigned int keybits)
{
    if (!ctx || !key)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    if (cipher != MBEDTLS_CIPHER_ID_AES)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    if (keybits != 128 && keybits != 192 && keybits != 256)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    hal_rvv_memcpy(ctx->key, key, keybits / 8);
    ctx->keybits = keybits;
    ctx->has_key = 1;
    ctx->started = 0;
    return 0;
}

/* Try to call HW init once we have both IV and lengths */
static int ccm_hw_init(mbedtls_ccm_context *ctx)
{
    drv_pufs_inst *dev;

    if (!ctx->have_iv || !ctx->have_lengths)
        return 0; /* Not ready yet, wait for more info */

    dev = ccm_get_dev();
    if (!dev)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    int ret = drv_pufs_cipher_ccm_init(&ctx->cipher, dev, SK_AES,
                                       ctx->mode == MBEDTLS_CCM_ENCRYPT,
                                       KT_SWKEY, ctx->key, ctx->keybits,
                                       ctx->iv, (uint32_t)ctx->iv_len,
                                       (uint64_t)ctx->total_ad_len,
                                       (uint64_t)ctx->plaintext_len,
                                       (uint32_t)ctx->tag_len);
    if (ret != 0)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    ctx->started = 1;
    return 0;
}

int mbedtls_ccm_starts(mbedtls_ccm_context *ctx,
                       int mode,
                       const unsigned char *iv,
                       size_t iv_len)
{
    if (!ctx || !ctx->has_key || !iv)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    if (mode != MBEDTLS_CCM_ENCRYPT && mode != MBEDTLS_CCM_DECRYPT &&
        mode != MBEDTLS_CCM_STAR_ENCRYPT && mode != MBEDTLS_CCM_STAR_DECRYPT)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    /* CCM nonce length must be 7..13 */
    if (iv_len < 7 || iv_len > 13)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    ctx->mode = mode;
    hal_rvv_memcpy(ctx->iv, iv, iv_len);
    ctx->iv_len = iv_len;
    ctx->have_iv = 1;
    ctx->started = 0;
    ctx->have_lengths = 0;

    /* If lengths were set before starts(), try init */
    return ccm_hw_init(ctx);
}

int mbedtls_ccm_set_lengths(mbedtls_ccm_context *ctx,
                            size_t total_ad_len,
                            size_t plaintext_len,
                            size_t tag_len)
{
    if (!ctx)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    /* tag_len must be 4, 6, 8, 10, 12, 14, or 16 for standard CCM */
    if (tag_len == 2 || tag_len % 2 != 0 || tag_len > 16 || tag_len == 0)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    ctx->total_ad_len = total_ad_len;
    ctx->plaintext_len = plaintext_len;
    ctx->tag_len = tag_len;
    ctx->have_lengths = 1;

    /* If IV was set before lengths, try init */
    return ccm_hw_init(ctx);
}

int mbedtls_ccm_update_ad(mbedtls_ccm_context *ctx,
                          const unsigned char *ad,
                          size_t ad_len)
{
    uint32_t outlen = 0;

    if (!ctx || !ctx->started)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    if (ad_len == 0)
        return 0;

    if (!ad)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    /* Pass AAD with NULL output — firmware detects AAD mode */
    int ret = drv_pufs_cipher_update(&ctx->cipher,
                                     NULL, &outlen,
                                     ad, (uint32_t)ad_len);
    if (ret != 0)
        return ccm_map_cipher_error(ctx, ret);

    return 0;
}

int mbedtls_ccm_update(mbedtls_ccm_context *ctx,
                       const unsigned char *input, size_t input_len,
                       unsigned char *output, size_t output_size,
                       size_t *output_len)
{
    uint32_t outlen = 0;

    if (!ctx || !ctx->started)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    if (output_len)
        *output_len = 0;

    if (input_len == 0)
        return 0;

    if (!input || !output)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    if (output_size < input_len)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    int ret = drv_pufs_cipher_update(&ctx->cipher,
                                     output, &outlen,
                                     input, (uint32_t)input_len);
    if (ret != 0)
        return ccm_map_cipher_error(ctx, ret);

    if (output_len)
        *output_len = outlen;

    return 0;
}

int mbedtls_ccm_finish(mbedtls_ccm_context *ctx,
                       unsigned char *tag, size_t tag_len)
{
    uint32_t outlen = 0;
    int ret;

    if (!ctx || !ctx->started)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    ret = drv_pufs_cipher_final(&ctx->cipher,
                                NULL, &outlen,
                                tag, (uint32_t)tag_len);

    ctx->started = 0;

    if (ret != 0)
        return ccm_map_cipher_error(ctx, ret);

    return 0;
}

/* One-shot CCM* encrypt and tag */
int mbedtls_ccm_star_encrypt_and_tag(mbedtls_ccm_context *ctx,
                                     size_t length,
                                     const unsigned char *iv, size_t iv_len,
                                     const unsigned char *ad, size_t ad_len,
                                     const unsigned char *input,
                                     unsigned char *output,
                                     unsigned char *tag, size_t tag_len)
{
    if (!ctx)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    return ccm_run_stream(ctx, MBEDTLS_CCM_STAR_ENCRYPT, length,
                          iv, iv_len, ad, ad_len,
                          input, output, tag, tag_len);
}

/* One-shot CCM encrypt and tag (standard, tag_len must be valid) */
int mbedtls_ccm_encrypt_and_tag(mbedtls_ccm_context *ctx, size_t length,
                                const unsigned char *iv, size_t iv_len,
                                const unsigned char *ad, size_t ad_len,
                                const unsigned char *input, unsigned char *output,
                                unsigned char *tag, size_t tag_len)
{
    if (!ctx)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    return ccm_run_stream(ctx, MBEDTLS_CCM_ENCRYPT, length,
                          iv, iv_len, ad, ad_len,
                          input, output, tag, tag_len);
}

/* One-shot CCM* auth decrypt */
int mbedtls_ccm_star_auth_decrypt(mbedtls_ccm_context *ctx, size_t length,
                                  const unsigned char *iv, size_t iv_len,
                                  const unsigned char *ad, size_t ad_len,
                                  const unsigned char *input, unsigned char *output,
                                  const unsigned char *tag, size_t tag_len)
{
    unsigned char tag_copy[16];

    if (!ctx)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    if (tag_len > 16)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    hal_rvv_memcpy(tag_copy, tag, tag_len);

    int ret = ccm_run_stream(ctx, MBEDTLS_CCM_STAR_DECRYPT, length,
                             iv, iv_len, ad, ad_len,
                             input, output, tag_copy, tag_len);
    if (ret != 0) {
        hal_rvv_memset(output, 0, length);
        return ret;
    }

    return 0;
}

/* One-shot CCM auth decrypt (standard) */
int mbedtls_ccm_auth_decrypt(mbedtls_ccm_context *ctx, size_t length,
                             const unsigned char *iv, size_t iv_len,
                             const unsigned char *ad, size_t ad_len,
                             const unsigned char *input, unsigned char *output,
                             const unsigned char *tag, size_t tag_len)
{
    unsigned char tag_copy[16];

    if (!ctx)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    if (tag_len > 16)
        return MBEDTLS_ERR_CCM_BAD_INPUT;

    hal_rvv_memcpy(tag_copy, tag, tag_len);

    int ret = ccm_run_stream(ctx, MBEDTLS_CCM_DECRYPT, length,
                             iv, iv_len, ad, ad_len,
                             input, output, tag_copy, tag_len);
    if (ret != 0) {
        hal_rvv_memset(output, 0, length);
        return ret;
    }

    return 0;
}

#endif /* MBEDTLS_CCM_ALT */
