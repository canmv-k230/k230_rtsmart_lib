/* PUF Secure Engine SHA-512 ALT for MbedTLS
 * Copyright (c) 2023, Canaan Bright Sight Co., Ltd
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef MBEDTLS_SHA512_ALT_H
#define MBEDTLS_SHA512_ALT_H

#include <stdint.h>
#include "drv_pufs.h"

typedef struct mbedtls_sha512_context {
    drv_pufs_inst dev;
    drv_pufs_hash_inst hash;
    int is384;
    int started;
} mbedtls_sha512_context;

#endif /* MBEDTLS_SHA512_ALT_H */
