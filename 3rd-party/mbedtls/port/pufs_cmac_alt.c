/* PUF Secure Engine CMAC ALT implementation for MbedTLS
 * Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 * SPDX-License-Identifier: BSD-2-Clause
 */

/* Allow access to cipher_context_t private members (cipher_info, cmac_ctx) */
#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "mbedtls/cmac.h"

#if defined(MBEDTLS_CMAC_ALT)

#include <string.h>
#include "mbedtls/cipher.h"
#include "mbedtls/cmac.h"
#include "mbedtls/error.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_util.h"
#include "drv_pufs.h"
#include <pthread.h>

/* ---- shared device handle ---- */
static drv_pufs_inst s_cmac_dev;
static int s_cmac_dev_ready = 0;
static pthread_once_t s_cmac_once = PTHREAD_ONCE_INIT;

static void cmac_dev_init_once(void)
{
    if (drv_pufs_open(&s_cmac_dev) == 0)
        s_cmac_dev_ready = 1;
}

static drv_pufs_inst *cmac_get_dev(void)
{
    pthread_once(&s_cmac_once, cmac_dev_init_once);
    return s_cmac_dev_ready ? &s_cmac_dev : NULL;
}

static uint32_t cmac_get_uint32_be(const unsigned char *input)
{
    return ((uint32_t) input[0] << 24) |
           ((uint32_t) input[1] << 16) |
           ((uint32_t) input[2] << 8) |
           (uint32_t) input[3];
}

static void cmac_put_uint32_be(uint32_t value, unsigned char *output)
{
    output[0] = (unsigned char) (value >> 24);
    output[1] = (unsigned char) (value >> 16);
    output[2] = (unsigned char) (value >> 8);
    output[3] = (unsigned char) value;
}

static int cmac_sw_multiply_by_u(unsigned char *output,
                                 const unsigned char *input,
                                 size_t block_size)
{
    unsigned char r_n;
    uint32_t overflow = 0;

    if (block_size == MBEDTLS_AES_BLOCK_SIZE) {
        r_n = 0x87;
    } else if (block_size == MBEDTLS_DES3_BLOCK_SIZE) {
        r_n = 0x1B;
    } else {
        return MBEDTLS_ERR_CIPHER_BAD_INPUT_DATA;
    }

    for (int index = (int) block_size - 4; index >= 0; index -= 4) {
        uint32_t value = cmac_get_uint32_be(input + index);
        uint32_t new_overflow = value >> 31;
        value = (value << 1) | overflow;
        cmac_put_uint32_be(value, output + index);
        overflow = new_overflow;
    }

    if (input[0] >> 7)
        output[block_size - 1] ^= r_n;

    return 0;
}

static void cmac_sw_pad(unsigned char *output,
                        size_t block_size,
                        const unsigned char *input,
                        size_t input_len)
{
    size_t index;

    for (index = 0; index < block_size; index++) {
        if (index < input_len)
            output[index] = input[index];
        else if (index == input_len)
            output[index] = 0x80;
        else
            output[index] = 0x00;
    }
}

static void cmac_sw_xor(unsigned char *dst,
                        const unsigned char *src,
                        size_t len)
{
    for (size_t index = 0; index < len; index++)
        dst[index] ^= src[index];
}

static int cmac_sw_generate_subkeys(mbedtls_cipher_context_t *ctx,
                                    unsigned char *k1,
                                    unsigned char *k2)
{
    unsigned char l[MBEDTLS_CMAC_MAX_BLOCK_SIZE];
    size_t olen = 0;
    size_t block_size = mbedtls_cipher_info_get_block_size(ctx->cipher_info);
    int ret;

    hal_rvv_memset(l, 0, sizeof(l));
    ret = mbedtls_cipher_update(ctx, l, block_size, l, &olen);
    if (ret != 0)
        return ret;

    ret = cmac_sw_multiply_by_u(k1, l, block_size);
    if (ret == 0)
        ret = cmac_sw_multiply_by_u(k2, k1, block_size);

    mbedtls_platform_zeroize(l, sizeof(l));
    return ret;
}

static int cmac_sw_starts(mbedtls_cipher_context_t *ctx,
                          mbedtls_cmac_context_t *cmac_ctx)
{
    cmac_ctx->use_sw = 1;
    cmac_ctx->hw_ready = 0;
    cmac_ctx->unprocessed_len = 0;
    mbedtls_platform_zeroize(cmac_ctx->state, sizeof(cmac_ctx->state));
    mbedtls_platform_zeroize(cmac_ctx->unprocessed_block, sizeof(cmac_ctx->unprocessed_block));
    return 0;
}

static int cmac_sw_update(mbedtls_cipher_context_t *ctx,
                          mbedtls_cmac_context_t *cmac_ctx,
                          const unsigned char *input,
                          size_t ilen)
{
    unsigned char block[MBEDTLS_CMAC_MAX_BLOCK_SIZE];
    size_t block_size = mbedtls_cipher_info_get_block_size(ctx->cipher_info);
    size_t n_blocks;
    size_t olen;
    int ret = 0;

    if (cmac_ctx->unprocessed_len > 0 && ilen > block_size - cmac_ctx->unprocessed_len) {
        hal_rvv_memcpy(&cmac_ctx->unprocessed_block[cmac_ctx->unprocessed_len],
               input, block_size - cmac_ctx->unprocessed_len);
        hal_rvv_memcpy(block, cmac_ctx->unprocessed_block, block_size);
        cmac_sw_xor(block, cmac_ctx->state, block_size);
        ret = mbedtls_cipher_update(ctx, block, block_size, cmac_ctx->state, &olen);
        if (ret != 0)
            return ret;

        input += block_size - cmac_ctx->unprocessed_len;
        ilen -= block_size - cmac_ctx->unprocessed_len;
        cmac_ctx->unprocessed_len = 0;
    }

    n_blocks = (ilen + block_size - 1) / block_size;
    for (size_t index = 1; index < n_blocks; index++) {
        hal_rvv_memcpy(block, input, block_size);
        cmac_sw_xor(block, cmac_ctx->state, block_size);
        ret = mbedtls_cipher_update(ctx, block, block_size, cmac_ctx->state, &olen);
        if (ret != 0)
            return ret;

        ilen -= block_size;
        input += block_size;
    }

    if (ilen > 0) {
        hal_rvv_memcpy(&cmac_ctx->unprocessed_block[cmac_ctx->unprocessed_len], input, ilen);
        cmac_ctx->unprocessed_len += ilen;
    }

    return 0;
}

static int cmac_sw_finish(mbedtls_cipher_context_t *ctx,
                          mbedtls_cmac_context_t *cmac_ctx,
                          unsigned char *output)
{
    unsigned char k1[MBEDTLS_CMAC_MAX_BLOCK_SIZE];
    unsigned char k2[MBEDTLS_CMAC_MAX_BLOCK_SIZE];
    unsigned char m_last[MBEDTLS_CMAC_MAX_BLOCK_SIZE];
    unsigned char state[MBEDTLS_CMAC_MAX_BLOCK_SIZE];
    size_t block_size = mbedtls_cipher_info_get_block_size(ctx->cipher_info);
    size_t olen = 0;
    int ret;

    hal_rvv_memset(k1, 0, sizeof(k1));
    hal_rvv_memset(k2, 0, sizeof(k2));
    hal_rvv_memset(m_last, 0, sizeof(m_last));
    hal_rvv_memset(state, 0, sizeof(state));

    ret = cmac_sw_generate_subkeys(ctx, k1, k2);
    if (ret != 0)
        goto cleanup;

    if (cmac_ctx->unprocessed_len < block_size) {
        cmac_sw_pad(m_last, block_size, cmac_ctx->unprocessed_block, cmac_ctx->unprocessed_len);
        cmac_sw_xor(m_last, k2, block_size);
    } else {
        hal_rvv_memcpy(m_last, cmac_ctx->unprocessed_block, block_size);
        cmac_sw_xor(m_last, k1, block_size);
    }

    hal_rvv_memcpy(state, cmac_ctx->state, block_size);
    cmac_sw_xor(state, m_last, block_size);
    ret = mbedtls_cipher_update(ctx, state, block_size, state, &olen);
    if (ret == 0)
        hal_rvv_memcpy(output, state, block_size);

cleanup:
    cmac_ctx->unprocessed_len = 0;
    mbedtls_platform_zeroize(cmac_ctx->state, sizeof(cmac_ctx->state));
    mbedtls_platform_zeroize(cmac_ctx->unprocessed_block, sizeof(cmac_ctx->unprocessed_block));
    mbedtls_platform_zeroize(k1, sizeof(k1));
    mbedtls_platform_zeroize(k2, sizeof(k2));
    mbedtls_platform_zeroize(m_last, sizeof(m_last));
    mbedtls_platform_zeroize(state, sizeof(state));
    return ret;
}

/* ---- helper: init PUF CMAC instance from stored key ---- */
static int cmac_hw_init(mbedtls_cmac_context_t *cmac_ctx)
{
    drv_pufs_inst *dev = cmac_get_dev();
    if (!dev)
        return MBEDTLS_ERR_CIPHER_FEATURE_UNAVAILABLE;

    int ret = drv_pufs_cmac_init(&cmac_ctx->cmac, dev,
                                 SK_AES, KT_SWKEY,
                                 cmac_ctx->key, cmac_ctx->keybits);
    if (ret != 0)
        return MBEDTLS_ERR_CIPHER_FEATURE_UNAVAILABLE;

    cmac_ctx->hw_ready = 1;
    cmac_ctx->use_sw = 0;
    return 0;
}

/*
 * mbedtls_cipher_cmac_starts - set the CMAC key and prepare for operation
 */
int mbedtls_cipher_cmac_starts(mbedtls_cipher_context_t *ctx,
                                const unsigned char *key, size_t keybits)
{
    mbedtls_cipher_type_t type;
    mbedtls_cmac_context_t *cmac_ctx;
    int ret;

    if (ctx == NULL || ctx->cipher_info == NULL || key == NULL)
        return MBEDTLS_ERR_CIPHER_BAD_INPUT_DATA;

    /* Set the cipher key through the normal path so the cipher context
     * remains consistent (some callers inspect cipher_info->type later). */
    ret = mbedtls_cipher_setkey(ctx, key, (int) keybits, MBEDTLS_ENCRYPT);
    if (ret != 0)
        return ret;

    type = mbedtls_cipher_info_get_type(ctx->cipher_info);
    switch (type) {
        case MBEDTLS_CIPHER_AES_128_ECB:
        case MBEDTLS_CIPHER_AES_192_ECB:
        case MBEDTLS_CIPHER_AES_256_ECB:
            break;
#if defined(MBEDTLS_DES_C)
        case MBEDTLS_CIPHER_DES_EDE3_ECB:
            break;
#endif
        default:
            return MBEDTLS_ERR_CIPHER_BAD_INPUT_DATA;
    }

    /* Allocate (or reuse) our ALT context */
    if (ctx->cmac_ctx == NULL) {
        cmac_ctx = mbedtls_calloc(1, sizeof(mbedtls_cmac_context_t));
        if (cmac_ctx == NULL)
            return MBEDTLS_ERR_CIPHER_ALLOC_FAILED;
        ctx->cmac_ctx = cmac_ctx;
    } else {
        cmac_ctx = ctx->cmac_ctx;
    }

    /* Store key for later re-init (reset path) */
    hal_rvv_memset(cmac_ctx, 0, sizeof(*cmac_ctx));
    hal_rvv_memcpy(cmac_ctx->key, key, (keybits + 7) / 8);
    cmac_ctx->keybits = (uint32_t) keybits;

    if (type == MBEDTLS_CIPHER_DES_EDE3_ECB) {
        return cmac_sw_starts(ctx, cmac_ctx);
    }

    /* Initialise PUF hardware CMAC */
    ret = cmac_hw_init(cmac_ctx);
    if (ret != 0)
        return ret;

    return 0;
}

/*
 * mbedtls_cipher_cmac_update - feed data to CMAC
 */
int mbedtls_cipher_cmac_update(mbedtls_cipher_context_t *ctx,
                                const unsigned char *input, size_t ilen)
{
    mbedtls_cmac_context_t *cmac_ctx;

    if (ctx == NULL || ctx->cmac_ctx == NULL)
        return MBEDTLS_ERR_CIPHER_BAD_INPUT_DATA;

    cmac_ctx = ctx->cmac_ctx;

    if (cmac_ctx->use_sw)
        return cmac_sw_update(ctx, cmac_ctx, input, ilen);

    if (!cmac_ctx->hw_ready)
        return MBEDTLS_ERR_CIPHER_FEATURE_UNAVAILABLE;

    if (ilen == 0)
        return 0;

    int ret = drv_pufs_cmac_update(&cmac_ctx->cmac, input, (uint32_t) ilen);
    if (ret != 0)
        return MBEDTLS_ERR_CIPHER_FEATURE_UNAVAILABLE;

    return 0;
}

/*
 * mbedtls_cipher_cmac_finish - produce the CMAC tag
 */
int mbedtls_cipher_cmac_finish(mbedtls_cipher_context_t *ctx,
                                unsigned char *output)
{
    mbedtls_cmac_context_t *cmac_ctx;
    uint32_t dlen = 0;

    if (ctx == NULL || ctx->cmac_ctx == NULL || output == NULL)
        return MBEDTLS_ERR_CIPHER_BAD_INPUT_DATA;

    cmac_ctx = ctx->cmac_ctx;

    if (cmac_ctx->use_sw)
        return cmac_sw_finish(ctx, cmac_ctx, output);

    if (!cmac_ctx->hw_ready)
        return MBEDTLS_ERR_CIPHER_FEATURE_UNAVAILABLE;

    int ret = drv_pufs_cmac_final(&cmac_ctx->cmac, output, &dlen);
    cmac_ctx->hw_ready = 0;   /* session consumed */

    if (ret != 0)
        return MBEDTLS_ERR_CIPHER_FEATURE_UNAVAILABLE;

    return 0;
}

/*
 * mbedtls_cipher_cmac_reset - reset state so another CMAC can be computed
 *                             with the same key
 */
int mbedtls_cipher_cmac_reset(mbedtls_cipher_context_t *ctx)
{
    mbedtls_cmac_context_t *cmac_ctx;

    if (ctx == NULL || ctx->cmac_ctx == NULL)
        return MBEDTLS_ERR_CIPHER_BAD_INPUT_DATA;

    cmac_ctx = ctx->cmac_ctx;

    if (cmac_ctx->use_sw) {
        cmac_ctx->unprocessed_len = 0;
        mbedtls_platform_zeroize(cmac_ctx->state, sizeof(cmac_ctx->state));
        mbedtls_platform_zeroize(cmac_ctx->unprocessed_block, sizeof(cmac_ctx->unprocessed_block));
        return 0;
    }

    /* Re-initialise the PUF CMAC with the stored key */
    return cmac_hw_init(cmac_ctx);
}

/*
 * mbedtls_cipher_cmac - one-shot CMAC computation
 */
int mbedtls_cipher_cmac(const mbedtls_cipher_info_t *cipher_info,
                         const unsigned char *key, size_t keylen,
                         const unsigned char *input, size_t ilen,
                         unsigned char *output)
{
    if (cipher_info == NULL || key == NULL || output == NULL)
        return MBEDTLS_ERR_CIPHER_BAD_INPUT_DATA;

    mbedtls_cipher_context_t ctx;
    int ret;

    mbedtls_cipher_init(&ctx);
    ret = mbedtls_cipher_setup(&ctx, cipher_info);
    if (ret == 0)
        ret = mbedtls_cipher_cmac_starts(&ctx, key, keylen);
    if (ret == 0)
        ret = mbedtls_cipher_cmac_update(&ctx, input, ilen);
    if (ret == 0)
        ret = mbedtls_cipher_cmac_finish(&ctx, output);
    mbedtls_cipher_free(&ctx);
    return ret;
}

#if defined(MBEDTLS_AES_C)
/*
 * mbedtls_aes_cmac_prf_128 - AES-CMAC-PRF-128 per RFC 4615
 *
 * If key_length == 16 use the key directly as K.
 * Otherwise compute K = AES-CMAC(zeros, key).
 * Then output = AES-CMAC(K, input).
 */
int mbedtls_aes_cmac_prf_128(const unsigned char *key, size_t key_length,
                              const unsigned char *input, size_t in_len,
                              unsigned char output[16])
{
    const mbedtls_cipher_info_t *cipher_info;
    unsigned char zero_key[16];
    unsigned char int_key[16];
    int ret;

    if (key == NULL || input == NULL || output == NULL)
        return MBEDTLS_ERR_CIPHER_BAD_INPUT_DATA;

    cipher_info = mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB);
    if (cipher_info == NULL)
        return MBEDTLS_ERR_CIPHER_BAD_INPUT_DATA;

    if (key_length == 16) {
        hal_rvv_memcpy(int_key, key, 16);
    } else {
        hal_rvv_memset(zero_key, 0, 16);
        ret = mbedtls_cipher_cmac(cipher_info, zero_key, 128,
                                  key, key_length, int_key);
        if (ret != 0)
            goto exit;
    }

    ret = mbedtls_cipher_cmac(cipher_info, int_key, 128,
                              input, in_len, output);
exit:
    mbedtls_platform_zeroize(int_key, sizeof(int_key));
    return ret;
}
#endif /* MBEDTLS_AES_C */

#endif /* MBEDTLS_CMAC_ALT */
