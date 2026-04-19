/* PUF Secure Engine SHA-256 ALT for MbedTLS
 * Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef MBEDTLS_SHA256_ALT_H
#define MBEDTLS_SHA256_ALT_H

#include <stdint.h>
#include "drv_pufs.h"
#include "pufs_sha256_sw.h"

#define MBEDTLS_PUFS_SHA256_SW_FALLBACK_THRESHOLD 256u

typedef enum {
    MBEDTLS_PUFS_SHA256_MODE_BUFFERED = 0,
    MBEDTLS_PUFS_SHA256_MODE_HW = 1,
} mbedtls_pufs_sha256_mode_t;

typedef struct mbedtls_sha256_context {
    mbedtls_sha256_sw_context sw;
    drv_pufs_hash_inst hash;
    int is224;
    int started;
    uint32_t mode;
    uint32_t buffered_len;
    unsigned char buffered[MBEDTLS_PUFS_SHA256_SW_FALLBACK_THRESHOLD];
} mbedtls_sha256_context;

#endif /* MBEDTLS_SHA256_ALT_H */
