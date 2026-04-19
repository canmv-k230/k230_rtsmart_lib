#ifndef MBEDTLS_PUFS_AES_SW_H
#define MBEDTLS_PUFS_AES_SW_H

#include <stddef.h>
#include <stdint.h>

typedef struct mbedtls_aes_sw_context {
    int nr;
    size_t rk_offset;
    uint32_t buf[68];
} mbedtls_aes_sw_context;

#if defined(MBEDTLS_CIPHER_MODE_XTS)
typedef struct mbedtls_aes_sw_xts_context {
    mbedtls_aes_sw_context crypt;
    mbedtls_aes_sw_context tweak;
} mbedtls_aes_sw_xts_context;
#endif

void mbedtls_aes_sw_init(mbedtls_aes_sw_context *ctx);
void mbedtls_aes_sw_free(mbedtls_aes_sw_context *ctx);
int mbedtls_aes_sw_setkey_enc(mbedtls_aes_sw_context *ctx,
                              const unsigned char *key,
                              unsigned int keybits);
int mbedtls_aes_sw_setkey_dec(mbedtls_aes_sw_context *ctx,
                              const unsigned char *key,
                              unsigned int keybits);
int mbedtls_internal_aes_sw_encrypt(mbedtls_aes_sw_context *ctx,
                                    const unsigned char input[16],
                                    unsigned char output[16]);
int mbedtls_internal_aes_sw_decrypt(mbedtls_aes_sw_context *ctx,
                                    const unsigned char input[16],
                                    unsigned char output[16]);
int mbedtls_aes_sw_crypt_ecb(mbedtls_aes_sw_context *ctx,
                             int mode,
                             const unsigned char input[16],
                             unsigned char output[16]);

#if defined(MBEDTLS_CIPHER_MODE_CBC)
int mbedtls_aes_sw_crypt_cbc(mbedtls_aes_sw_context *ctx,
                             int mode,
                             size_t length,
                             unsigned char iv[16],
                             const unsigned char *input,
                             unsigned char *output);
#endif

#if defined(MBEDTLS_CIPHER_MODE_XTS)
void mbedtls_aes_sw_xts_init(mbedtls_aes_sw_xts_context *ctx);
void mbedtls_aes_sw_xts_free(mbedtls_aes_sw_xts_context *ctx);
int mbedtls_aes_sw_xts_setkey_enc(mbedtls_aes_sw_xts_context *ctx,
                                  const unsigned char *key,
                                  unsigned int keybits);
int mbedtls_aes_sw_xts_setkey_dec(mbedtls_aes_sw_xts_context *ctx,
                                  const unsigned char *key,
                                  unsigned int keybits);
int mbedtls_aes_sw_crypt_xts(mbedtls_aes_sw_xts_context *ctx,
                             int mode,
                             size_t length,
                             const unsigned char data_unit[16],
                             const unsigned char *input,
                             unsigned char *output);
#endif

#if defined(MBEDTLS_CIPHER_MODE_CFB)
int mbedtls_aes_sw_crypt_cfb128(mbedtls_aes_sw_context *ctx,
                                int mode,
                                size_t length,
                                size_t *iv_off,
                                unsigned char iv[16],
                                const unsigned char *input,
                                unsigned char *output);
int mbedtls_aes_sw_crypt_cfb8(mbedtls_aes_sw_context *ctx,
                              int mode,
                              size_t length,
                              unsigned char iv[16],
                              const unsigned char *input,
                              unsigned char *output);
#endif

#if defined(MBEDTLS_CIPHER_MODE_OFB)
int mbedtls_aes_sw_crypt_ofb(mbedtls_aes_sw_context *ctx,
                             size_t length,
                             size_t *iv_off,
                             unsigned char iv[16],
                             const unsigned char *input,
                             unsigned char *output);
#endif

#if defined(MBEDTLS_CIPHER_MODE_CTR)
int mbedtls_aes_sw_crypt_ctr(mbedtls_aes_sw_context *ctx,
                             size_t length,
                             size_t *nc_off,
                             unsigned char nonce_counter[16],
                             unsigned char stream_block[16],
                             const unsigned char *input,
                             unsigned char *output);
#endif

#endif /* MBEDTLS_PUFS_AES_SW_H */
