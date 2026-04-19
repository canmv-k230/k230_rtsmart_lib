/* PUF Secure Engine AES ALT implementation for MbedTLS
 * Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "mbedtls/aes.h"

#if defined(MBEDTLS_AES_ALT)

#include <string.h>
#include <pthread.h>

#include "drv_pufs.h"
#include "hal_rvv_ops.h"

static drv_pufs_inst s_aes_dev;
static int s_aes_dev_ready = 0;
static pthread_once_t s_aes_once = PTHREAD_ONCE_INIT;

static void aes_dev_init_once(void)
{
    if (drv_pufs_open(&s_aes_dev) == 0)
        s_aes_dev_ready = 1;
}

static drv_pufs_inst *aes_get_dev(void)
{
    pthread_once(&s_aes_once, aes_dev_init_once);
    return s_aes_dev_ready ? &s_aes_dev : NULL;
}

static int aes_validate_keybits(unsigned int keybits)
{
    return (keybits == 128 || keybits == 192 || keybits == 256);
}

static void aes_increment_counter(unsigned char counter[16])
{
    int index;

    for (index = 15; index >= 0; index--) {
        counter[index]++;
        if (counter[index] != 0)
            break;
    }
}

/* Add n to a 128-bit big-endian counter in O(1) instead of looping. */
static void aes_add_counter(unsigned char counter[16], size_t n)
{
    uint64_t lo, new_lo;

    lo = ((uint64_t)counter[8]  << 56) | ((uint64_t)counter[9]  << 48) |
         ((uint64_t)counter[10] << 40) | ((uint64_t)counter[11] << 32) |
         ((uint64_t)counter[12] << 24) | ((uint64_t)counter[13] << 16) |
         ((uint64_t)counter[14] << 8)  | (uint64_t)counter[15];

    new_lo = lo + (uint64_t)n;

    counter[8]  = (unsigned char)(new_lo >> 56);
    counter[9]  = (unsigned char)(new_lo >> 48);
    counter[10] = (unsigned char)(new_lo >> 40);
    counter[11] = (unsigned char)(new_lo >> 32);
    counter[12] = (unsigned char)(new_lo >> 24);
    counter[13] = (unsigned char)(new_lo >> 16);
    counter[14] = (unsigned char)(new_lo >> 8);
    counter[15] = (unsigned char)new_lo;

    if (new_lo < lo) {
        uint64_t hi;
        hi = ((uint64_t)counter[0] << 56) | ((uint64_t)counter[1] << 48) |
             ((uint64_t)counter[2] << 40) | ((uint64_t)counter[3] << 32) |
             ((uint64_t)counter[4] << 24) | ((uint64_t)counter[5] << 16) |
             ((uint64_t)counter[6] << 8)  | (uint64_t)counter[7];
        hi++;
        counter[0] = (unsigned char)(hi >> 56);
        counter[1] = (unsigned char)(hi >> 48);
        counter[2] = (unsigned char)(hi >> 40);
        counter[3] = (unsigned char)(hi >> 32);
        counter[4] = (unsigned char)(hi >> 24);
        counter[5] = (unsigned char)(hi >> 16);
        counter[6] = (unsigned char)(hi >> 8);
        counter[7] = (unsigned char)hi;
    }
}

static int aes_hw_crypt_sp38a(mbedtls_aes_context *ctx,
                              pufs_skcipher_mode_t hw_mode,
                              int encrypt,
                              const unsigned char *iv,
                              size_t iv_len,
                              const unsigned char *input,
                              size_t length,
                              unsigned char *output)
{
    drv_pufs_inst *dev;
    drv_pufs_cipher_inst cipher;
    unsigned char tail[2 * PUFS_BC_BLOCK_SIZE];
    uint32_t update_out = 0;
    uint32_t final_out = 0;
    int ret;

    dev = aes_get_dev();
    if (!dev)
        return -1;

    ret = drv_pufs_cipher_init(&cipher, dev, SK_AES, hw_mode, encrypt,
                               KT_SWKEY, ctx->key, ctx->keybits,
                               iv, (uint32_t) iv_len);
    if (ret != 0)
        return ret;

    ret = drv_pufs_cipher_update(&cipher, output, &update_out,
                                 input, (uint32_t) length);
    if (ret != 0)
        return ret;

    ret = drv_pufs_cipher_final(&cipher, tail, &final_out, NULL, 0);
    if (ret != 0)
        return ret;

    if ((size_t) update_out + final_out != length)
        return -1;

    if (final_out > 0)
        hal_rvv_memcpy(output + update_out, tail, final_out);

    return 0;
}

#if defined(MBEDTLS_CIPHER_MODE_CBC)
static int aes_hw_crypt_cbc(mbedtls_aes_context *ctx,
                            int mode,
                            size_t length,
                            unsigned char iv[16],
                            const unsigned char *input,
                            unsigned char *output)
{
    int ret;
    unsigned char saved_iv[16];

    /* Save the last ciphertext block BEFORE decryption, because input and
     * output may overlap (in-place decrypt) and the decrypt would overwrite
     * the ciphertext we need for the next IV. */
    if (length > 0 && mode == MBEDTLS_AES_DECRYPT)
        hal_rvv_memcpy(saved_iv, input + length - 16, 16);

    ret = aes_hw_crypt_sp38a(ctx, MODE_CBC, mode == MBEDTLS_AES_ENCRYPT,
                             iv, 16, input, length, output);
    if (ret != 0)
        return ret;

    if (length > 0) {
        if (mode == MBEDTLS_AES_ENCRYPT)
            hal_rvv_memcpy(iv, output + length - 16, 16);
        else
            hal_rvv_memcpy(iv, saved_iv, 16);
    }

    return 0;
}
#endif

#if defined(MBEDTLS_CIPHER_MODE_CTR)
static int aes_hw_crypt_ctr(mbedtls_aes_context *ctx,
                            size_t length,
                            size_t *nc_off,
                            unsigned char nonce_counter[16],
                            unsigned char stream_block[16],
                            const unsigned char *input,
                            unsigned char *output)
{
    unsigned char initial_counter[16];
    size_t blocks;
    size_t rem;
    int ret;

    hal_rvv_memcpy(initial_counter, nonce_counter, sizeof(initial_counter));

    ret = aes_hw_crypt_sp38a(ctx, MODE_CTR, 1,
                             nonce_counter, 16, input, length, output);
    if (ret != 0)
        return ret;

    blocks = (length + 15u) / 16u;
    rem = length & 0x0Fu;

    /* Advance nonce_counter by the number of blocks consumed. O(1). */
    hal_rvv_memcpy(nonce_counter, initial_counter, sizeof(initial_counter));
    aes_add_counter(nonce_counter, blocks);

    /* If the last block was partial, reconstruct stream_block so the caller
     * can resume from the mid-block offset on the next call. */
    if (rem != 0) {
        unsigned char last_counter[16];
        hal_rvv_memcpy(last_counter, initial_counter, sizeof(last_counter));
        aes_add_counter(last_counter, blocks - 1);

        ret = mbedtls_aes_sw_crypt_ecb(&ctx->sw, MBEDTLS_AES_ENCRYPT,
                                       last_counter, stream_block);
        if (ret != 0)
            return ret;
    }

    *nc_off = (*nc_off + length) & 0x0Fu;
    return 0;
}
#endif

void mbedtls_aes_init(mbedtls_aes_context *ctx)
{
    if (!ctx)
        return;

    hal_rvv_memset(ctx, 0, sizeof(*ctx));
    mbedtls_aes_sw_init(&ctx->sw);
}

void mbedtls_aes_free(mbedtls_aes_context *ctx)
{
    if (!ctx)
        return;

    mbedtls_aes_sw_free(&ctx->sw);
    hal_rvv_memset(ctx, 0, sizeof(*ctx));
}

int mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx, const unsigned char *key,
                           unsigned int keybits)
{
    int ret;

    if (!ctx || !key || !aes_validate_keybits(keybits))
        return MBEDTLS_ERR_AES_INVALID_KEY_LENGTH;

    ret = mbedtls_aes_sw_setkey_enc(&ctx->sw, key, keybits);
    if (ret != 0)
        return ret;

    hal_rvv_memcpy(ctx->key, key, keybits / 8);
    ctx->keybits = keybits;
    ctx->has_key = 1;
    return 0;
}

int mbedtls_aes_setkey_dec(mbedtls_aes_context *ctx, const unsigned char *key,
                           unsigned int keybits)
{
    int ret;

    if (!ctx || !key || !aes_validate_keybits(keybits))
        return MBEDTLS_ERR_AES_INVALID_KEY_LENGTH;

    ret = mbedtls_aes_sw_setkey_dec(&ctx->sw, key, keybits);
    if (ret != 0)
        return ret;

    hal_rvv_memcpy(ctx->key, key, keybits / 8);
    ctx->keybits = keybits;
    ctx->has_key = 1;
    return 0;
}

#if defined(MBEDTLS_CIPHER_MODE_XTS)
void mbedtls_aes_xts_init(mbedtls_aes_xts_context *ctx)
{
    if (!ctx)
        return;

    mbedtls_aes_init(&ctx->crypt);
    mbedtls_aes_init(&ctx->tweak);
}

void mbedtls_aes_xts_free(mbedtls_aes_xts_context *ctx)
{
    if (!ctx)
        return;

    mbedtls_aes_free(&ctx->crypt);
    mbedtls_aes_free(&ctx->tweak);
}

int mbedtls_aes_xts_setkey_enc(mbedtls_aes_xts_context *ctx,
                               const unsigned char *key,
                               unsigned int keybits)
{
    mbedtls_aes_sw_xts_context sw_ctx;
    int ret;

    if (!ctx || !key)
        return MBEDTLS_ERR_AES_INVALID_KEY_LENGTH;

    mbedtls_aes_sw_xts_init(&sw_ctx);
    ret = mbedtls_aes_sw_xts_setkey_enc(&sw_ctx, key, keybits);
    if (ret == 0) {
        ret = mbedtls_aes_setkey_enc(&ctx->crypt, key, keybits / 2);
        if (ret == 0)
            ret = mbedtls_aes_setkey_enc(&ctx->tweak,
                                         key + (keybits / 16), keybits / 2);
    }
    mbedtls_aes_sw_xts_free(&sw_ctx);
    return ret;
}

int mbedtls_aes_xts_setkey_dec(mbedtls_aes_xts_context *ctx,
                               const unsigned char *key,
                               unsigned int keybits)
{
    mbedtls_aes_sw_xts_context sw_ctx;
    int ret;

    if (!ctx || !key)
        return MBEDTLS_ERR_AES_INVALID_KEY_LENGTH;

    mbedtls_aes_sw_xts_init(&sw_ctx);
    ret = mbedtls_aes_sw_xts_setkey_dec(&sw_ctx, key, keybits);
    if (ret == 0) {
        ret = mbedtls_aes_setkey_dec(&ctx->crypt, key, keybits / 2);
        if (ret == 0)
            ret = mbedtls_aes_setkey_enc(&ctx->tweak,
                                         key + (keybits / 16), keybits / 2);
    }
    mbedtls_aes_sw_xts_free(&sw_ctx);
    return ret;
}

int mbedtls_aes_crypt_xts(mbedtls_aes_xts_context *ctx,
                          int mode,
                          size_t length,
                          const unsigned char data_unit[16],
                          const unsigned char *input,
                          unsigned char *output)
{
    mbedtls_aes_sw_xts_context sw_ctx;
    int ret;

    if (!ctx)
        return MBEDTLS_ERR_AES_BAD_INPUT_DATA;

    mbedtls_aes_sw_xts_init(&sw_ctx);
    ret = mbedtls_aes_sw_setkey_enc(&sw_ctx.tweak, ctx->tweak.key, ctx->tweak.keybits);
    if (ret == 0) {
        if (mode == MBEDTLS_AES_ENCRYPT)
            ret = mbedtls_aes_sw_setkey_enc(&sw_ctx.crypt, ctx->crypt.key, ctx->crypt.keybits);
        else
            ret = mbedtls_aes_sw_setkey_dec(&sw_ctx.crypt, ctx->crypt.key, ctx->crypt.keybits);
    }
    if (ret == 0)
        ret = mbedtls_aes_sw_crypt_xts(&sw_ctx, mode, length, data_unit, input, output);
    mbedtls_aes_sw_xts_free(&sw_ctx);
    return ret;
}
#endif

int mbedtls_internal_aes_encrypt(mbedtls_aes_context *ctx,
                                 const unsigned char input[16],
                                 unsigned char output[16])
{
    if (!ctx)
        return MBEDTLS_ERR_AES_BAD_INPUT_DATA;

    return mbedtls_internal_aes_sw_encrypt(&ctx->sw, input, output);
}

int mbedtls_internal_aes_decrypt(mbedtls_aes_context *ctx,
                                 const unsigned char input[16],
                                 unsigned char output[16])
{
    if (!ctx)
        return MBEDTLS_ERR_AES_BAD_INPUT_DATA;

    return mbedtls_internal_aes_sw_decrypt(&ctx->sw, input, output);
}

int mbedtls_aes_crypt_ecb(mbedtls_aes_context *ctx,
                          int mode,
                          const unsigned char input[16],
                          unsigned char output[16])
{
    if (!ctx)
        return MBEDTLS_ERR_AES_BAD_INPUT_DATA;

    return mbedtls_aes_sw_crypt_ecb(&ctx->sw, mode, input, output);
}

#if defined(MBEDTLS_CIPHER_MODE_CBC)
int mbedtls_aes_crypt_cbc(mbedtls_aes_context *ctx,
                          int mode,
                          size_t length,
                          unsigned char iv[16],
                          const unsigned char *input,
                          unsigned char *output)
{
    unsigned char iv_copy[16];

    if (!ctx)
        return MBEDTLS_ERR_AES_BAD_INPUT_DATA;

    if (length >= MBEDTLS_PUFS_AES_HW_THRESHOLD && ctx->has_key) {
        hal_rvv_memcpy(iv_copy, iv, sizeof(iv_copy));
        if (aes_hw_crypt_cbc(ctx, mode, length, iv_copy, input, output) == 0) {
            hal_rvv_memcpy(iv, iv_copy, sizeof(iv_copy));
            return 0;
        }
    }

    return mbedtls_aes_sw_crypt_cbc(&ctx->sw, mode, length, iv, input, output);
}
#endif

#if defined(MBEDTLS_CIPHER_MODE_CFB)
int mbedtls_aes_crypt_cfb128(mbedtls_aes_context *ctx,
                             int mode,
                             size_t length,
                             size_t *iv_off,
                             unsigned char iv[16],
                             const unsigned char *input,
                             unsigned char *output)
{
    if (!ctx)
        return MBEDTLS_ERR_AES_BAD_INPUT_DATA;

    return mbedtls_aes_sw_crypt_cfb128(&ctx->sw, mode, length, iv_off,
                                       iv, input, output);
}

int mbedtls_aes_crypt_cfb8(mbedtls_aes_context *ctx,
                           int mode,
                           size_t length,
                           unsigned char iv[16],
                           const unsigned char *input,
                           unsigned char *output)
{
    if (!ctx)
        return MBEDTLS_ERR_AES_BAD_INPUT_DATA;

    return mbedtls_aes_sw_crypt_cfb8(&ctx->sw, mode, length, iv, input, output);
}
#endif

#if defined(MBEDTLS_CIPHER_MODE_OFB)
int mbedtls_aes_crypt_ofb(mbedtls_aes_context *ctx,
                          size_t length,
                          size_t *iv_off,
                          unsigned char iv[16],
                          const unsigned char *input,
                          unsigned char *output)
{
    if (!ctx)
        return MBEDTLS_ERR_AES_BAD_INPUT_DATA;

    return mbedtls_aes_sw_crypt_ofb(&ctx->sw, length, iv_off, iv, input, output);
}
#endif

#if defined(MBEDTLS_CIPHER_MODE_CTR)
int mbedtls_aes_crypt_ctr(mbedtls_aes_context *ctx,
                          size_t length,
                          size_t *nc_off,
                          unsigned char nonce_counter[16],
                          unsigned char stream_block[16],
                          const unsigned char *input,
                          unsigned char *output)
{
    unsigned char nonce_copy[16];
    unsigned char stream_copy[16];
    size_t nc_off_copy;

    if (!ctx)
        return MBEDTLS_ERR_AES_BAD_INPUT_DATA;

    if (length >= MBEDTLS_PUFS_AES_HW_THRESHOLD && ctx->has_key &&
        nc_off != NULL && *nc_off == 0) {
        nc_off_copy = *nc_off;
        hal_rvv_memcpy(nonce_copy, nonce_counter, sizeof(nonce_copy));
        hal_rvv_memcpy(stream_copy, stream_block, sizeof(stream_copy));
        if (aes_hw_crypt_ctr(ctx, length, &nc_off_copy, nonce_copy,
                             stream_copy, input, output) == 0) {
            *nc_off = nc_off_copy;
            hal_rvv_memcpy(nonce_counter, nonce_copy, sizeof(nonce_copy));
            hal_rvv_memcpy(stream_block, stream_copy, sizeof(stream_copy));
            return 0;
        }
    }

    return mbedtls_aes_sw_crypt_ctr(&ctx->sw, length, nc_off,
                                    nonce_counter, stream_block,
                                    input, output);
}
#endif

#endif /* MBEDTLS_AES_ALT */