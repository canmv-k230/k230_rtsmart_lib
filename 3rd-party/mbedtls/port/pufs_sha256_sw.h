#ifndef MBEDTLS_PUFS_SHA256_SW_H
#define MBEDTLS_PUFS_SHA256_SW_H

#include <stddef.h>
#include <stdint.h>

typedef struct mbedtls_sha256_sw_context {
    unsigned char buffer[64];
    uint32_t total[2];
    uint32_t state[8];
    int is224;
} mbedtls_sha256_sw_context;

void mbedtls_sha256_sw_init(mbedtls_sha256_sw_context *ctx);
void mbedtls_sha256_sw_free(mbedtls_sha256_sw_context *ctx);
void mbedtls_sha256_sw_clone(mbedtls_sha256_sw_context *dst,
                             const mbedtls_sha256_sw_context *src);
int mbedtls_sha256_sw_starts(mbedtls_sha256_sw_context *ctx, int is224);
int mbedtls_sha256_sw_update(mbedtls_sha256_sw_context *ctx,
                             const unsigned char *input,
                             size_t ilen);
int mbedtls_sha256_sw_finish(mbedtls_sha256_sw_context *ctx,
                             unsigned char *output);
int mbedtls_internal_sha256_sw_process(mbedtls_sha256_sw_context *ctx,
                                       const unsigned char data[64]);
int mbedtls_sha256_sw(const unsigned char *input,
                      size_t ilen,
                      unsigned char *output,
                      int is224);
int mbedtls_sha256_sw_self_test(int verbose);
int mbedtls_sha224_sw_self_test(int verbose);

#endif /* MBEDTLS_PUFS_SHA256_SW_H */