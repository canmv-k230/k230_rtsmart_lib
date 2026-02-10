// https://tls.mbed.org/module-level-design-cipher
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "cipher_wrapper.h"
#include "tuya_cloud_types.h"
#include "log.h"

static int mbedtls_cipher_auth_encrypt(mbedtls_cipher_context_t *ctx,
                                       const unsigned char *iv, size_t iv_len,
                                       const unsigned char *ad, size_t ad_len,
                                       const unsigned char *input, size_t ilen,
                                       unsigned char *output, size_t *olen,
                                       unsigned char *tag, size_t tag_len)
{
    unsigned char *tmp = NULL;
    size_t tmp_len = ilen + tag_len;
    size_t tmp_olen = 0;
    int ret;

    if (output == NULL || olen == NULL) {
        return MBEDTLS_ERR_CIPHER_BAD_INPUT_DATA;
    }

    tmp = (unsigned char *)malloc(tmp_len);
    if (tmp == NULL) {
        return MBEDTLS_ERR_CIPHER_ALLOC_FAILED;
    }

    ret = mbedtls_cipher_auth_encrypt_ext(ctx,
                                          iv, iv_len,
                                          ad, ad_len,
                                          input, ilen,
                                          tmp, tmp_len,
                                          &tmp_olen, tag_len);
    if (ret != 0) {
        free(tmp);
        return ret;
    }
    if (tmp_olen < tag_len) {
        free(tmp);
        return MBEDTLS_ERR_CIPHER_BAD_INPUT_DATA;
    }

    *olen = tmp_olen - tag_len;
    memcpy(output, tmp, *olen);
    if (tag != NULL && tag_len > 0) {
        memcpy(tag, tmp + *olen, tag_len);
    }

    free(tmp);
    return 0;
}

static int mbedtls_cipher_auth_decrypt(mbedtls_cipher_context_t *ctx,
                                       const unsigned char *iv, size_t iv_len,
                                       const unsigned char *ad, size_t ad_len,
                                       const unsigned char *input, size_t ilen,
                                       unsigned char *output, size_t *olen,
                                       unsigned char *tag, size_t tag_len)
{
    unsigned char *tmp = NULL;
    size_t tmp_len = ilen + tag_len;
    int ret;

    if (output == NULL || olen == NULL) {
        return MBEDTLS_ERR_CIPHER_BAD_INPUT_DATA;
    }

    tmp = (unsigned char *)malloc(tmp_len);
    if (tmp == NULL) {
        return MBEDTLS_ERR_CIPHER_ALLOC_FAILED;
    }

    memcpy(tmp, input, ilen);
    if (tag != NULL && tag_len > 0) {
        memcpy(tmp + ilen, tag, tag_len);
    }

    ret = mbedtls_cipher_auth_decrypt_ext(ctx,
                                          iv, iv_len,
                                          ad, ad_len,
                                          tmp, tmp_len,
                                          output, ilen,
                                          olen, tag_len);
    free(tmp);
    return ret;
}

int mbedtls_cipher_auth_encrypt_wrapper(const cipher_params_t* input,
                                        unsigned char *output, size_t *olen,
                                        unsigned char *tag, size_t tag_len)
{
    if (input == NULL || output == NULL || olen == NULL) {
        return OPRT_INVALID_PARM;
    }

    int ret = OPRT_OK;

    mbedtls_cipher_info_t* cipher_info;
    mbedtls_cipher_context_t cipher_ctx;
    size_t key_bitlen = 0;

    mbedtls_cipher_init(&cipher_ctx);

    /*
   * Read the Cipher and MD from the command line
   */
    cipher_info = (mbedtls_cipher_info_t*)mbedtls_cipher_info_from_type(input->cipher_type);
    if (cipher_info == NULL) {
        log_error("Cipher not found\n");
        ret = OPRT_INVALID_PARM;
        goto EXIT;
    }
    
    if ((ret = mbedtls_cipher_setup(&cipher_ctx, cipher_info)) != 0) {
        log_error("mbedtls_cipher_setup failed\n");
        goto EXIT;
    }
    
    key_bitlen = mbedtls_cipher_info_get_key_bitlen(cipher_info);
    if (input->key_len * 8 != key_bitlen) {
        log_error("key_len:%d", input->key_len * 8);
        ret = OPRT_INVALID_PARM;
        goto EXIT;
    }

    if (ret = mbedtls_cipher_setkey(&cipher_ctx, input->key, (int)key_bitlen, MBEDTLS_ENCRYPT) != 0) {
        log_error("mbedtls_cipher_setkey() returned error\n");
        goto EXIT;
    }

    /*
   * Encrypt and write the ciphertext.
   */
    ret = mbedtls_cipher_auth_encrypt( &cipher_ctx,
                                        input->nonce, input->nonce_len,
                                        input->ad, input->ad_len,
                                        input->data, input->data_len,
                                        output, olen, 
                                        tag, tag_len);
EXIT:
    mbedtls_cipher_free(&cipher_ctx);
    return (ret);
}

int mbedtls_cipher_auth_decrypt_wrapper(const cipher_params_t* input,
                                        unsigned char *output, size_t *olen,
                                        unsigned char *tag, size_t tag_len)
{
    if (input == NULL || output == NULL || olen == NULL) {
        return OPRT_INVALID_PARM;
    }

    int ret = OPRT_OK;

    const mbedtls_cipher_info_t* cipher_info;
    mbedtls_cipher_context_t cipher_ctx;
    size_t key_bitlen = 0;

    mbedtls_cipher_init(&cipher_ctx);

    /*
   * Read the Cipher and MD from the command line
   */
    cipher_info = mbedtls_cipher_info_from_type(input->cipher_type);
    if (cipher_info == NULL) {
        log_error("Cipher '%s' not found\n",
            "mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_CBC)");
        goto EXIT;
    }

    if ((ret = mbedtls_cipher_setup(&cipher_ctx, cipher_info)) != 0) {
        log_error("mbedtls_cipher_setup failed\n");
        goto EXIT;
    }

    /*
   *  The encrypted file must be structured as follows:
   *
   *        00 .. 15              Initialization Vector
   *        16 .. 31              Encrypted Block #1
   *           ..
   *      N*16 .. (N+1)*16 - 1    Encrypted Block #N
   *  (N+1)*16 .. (N+1)*16 + n    Hash(ciphertext)
   */

    if (mbedtls_cipher_get_block_size(&cipher_ctx) == 0) {
        log_error("Invalid cipher block size: 0. \n");
        goto EXIT;
    }

    key_bitlen = mbedtls_cipher_info_get_key_bitlen(cipher_info);
    if (mbedtls_cipher_setkey(&cipher_ctx, input->key, (int)key_bitlen, MBEDTLS_DECRYPT) != 0) {
        log_error("mbedtls_cipher_setkey() returned error\n");
        goto EXIT;
    }

    /*
   * Decrypt and write the plaintext.
   */
    ret = mbedtls_cipher_auth_decrypt( &cipher_ctx,
                                        input->nonce, input->nonce_len,
                                        input->ad, input->ad_len,
                                        input->data, input->data_len,
                                        output, olen, 
                                        tag, tag_len);
EXIT:
    mbedtls_cipher_free(&cipher_ctx);
    return (ret);
}

int mbedtls_message_digest( mbedtls_md_type_t md_type, 
                            const uint8_t* input, size_t ilen, 
                            uint8_t* digest)
{
    if (input == NULL || ilen == 0 || digest == NULL)
        return -1;

    mbedtls_md_context_t md_ctx;
    mbedtls_md_init(&md_ctx);
    int ret = mbedtls_md_setup(&md_ctx, mbedtls_md_info_from_type(md_type), 0);
    if (ret != 0) {
        log_error("mbedtls_md_setup() returned -0x%04x\n", -ret);
        goto exit;
    }

    mbedtls_md_starts(&md_ctx);
    mbedtls_md_update(&md_ctx, input, ilen);
    mbedtls_md_finish(&md_ctx, digest);

exit:
    mbedtls_md_free(&md_ctx);
    return ret;
}

int mbedtls_message_digest_hmac(mbedtls_md_type_t md_type,
                                const uint8_t* key, size_t keylen,
                                const uint8_t* input, size_t ilen, 
                                uint8_t* digest)
    {
    if (key == NULL || keylen == 0 || input == NULL || ilen == 0 || digest == NULL)
        return -1;

    mbedtls_md_context_t md_ctx;
    mbedtls_md_init(&md_ctx);
    int ret = mbedtls_md_setup(&md_ctx, mbedtls_md_info_from_type(md_type), 1);
    if (ret != 0) {
        log_error("mbedtls_md_setup() returned -0x%04x\n", -ret);
        goto exit;
    }

    mbedtls_md_hmac_starts(&md_ctx, key, keylen);
    mbedtls_md_hmac_update(&md_ctx, input, ilen);
    mbedtls_md_hmac_finish(&md_ctx, digest);

exit:
    mbedtls_md_free(&md_ctx);
    return ret;
}
